/*
Copyright 2023 Google Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "service.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"
#include "command_utils.h"
#include "constants.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "log.h"
#include "message_handler.h"
#include "messages.h"
#include "serializable.h"
#include "socket_connection.h"
#include "tcp_server.h"
#include "trace_mgr.h"

namespace Dive
{

class ServerMessageHandler : public TransferInfra::IMessageHandler
{
public:
    ServerMessageHandler() {}
    void onConnect() override { LOGI("ServerMessageHandler: onConnect"); }
    void onDisconnect() override { LOGI("ServerMessageHandler: onDisconnect"); }

    void handleMessage(std::unique_ptr<TransferInfra::ISerializable> msg,
                       TransferInfra::SocketConnection              *clientConn) override
    {
        LOGI("ServerMessageHandler");

        auto tid = std::this_thread::get_id();
        LOGI("DefaultSrvHdlr (Thread:%d ): RX msg type %d", tid, (int)msg->getMessageType());
        if (!msg || !clientConn)
        {
            LOGI("Null msg/conn");
            return;
        }

        if (msg->getMessageType() ==
            static_cast<uint8_t>(TransferInfra::MessageType::HEARTBEAT_PING))
        {
            TransferInfra::HeartbeatMessage *ping = dynamic_cast<TransferInfra::HeartbeatMessage *>(
            msg.get());
            if (ping)
            {
                LOGI("HeartbeatMessage: ping");
                TransferInfra::HeartbeatResponseMessage pong;
                pong.original_timestamp_or_seq = ping->timestamp_or_seq;
                std::error_code ec_p;
                if (!sendMessage(clientConn, pong, ec_p))
                {
                    LOGI("Send PONG fail: %s", ec_p.message().c_str());
                }
            }
        }
        else if (msg->getMessageType() ==
                 static_cast<uint8_t>(TransferInfra::MessageType::HANDSHAKE))
        {
            TransferInfra::HandShakeMessage
            *hs_msg = dynamic_cast<TransferInfra::HandShakeMessage *>(msg.get());
            if (hs_msg)
            {
                LOGI("HandShakeMessage: handshake");
                TransferInfra::HandShakeMessage hs_response_msg;
                hs_response_msg.major_version = hs_msg->major_version;
                hs_response_msg.minor_version = hs_msg->minor_version;
                std::error_code ec;
                if (!sendMessage(clientConn, hs_response_msg, ec))
                {
                    LOGI("Send Handshake fail: %s", ec.message().c_str());
                }
            }
        }
        else if (msg->getMessageType() ==
                 static_cast<uint8_t>(TransferInfra::MessageType::CAPTURE_REQ))
        {
            GetTraceMgr().TriggerTrace();
            GetTraceMgr().WaitForTraceDone();
            std::string p = GetTraceMgr().GetTraceFilePath();

            TransferInfra::CaptureResponse response;
            response.data_field = p;
            LOGI("response.path: %s", p.c_str());
            std::error_code ec;
            if (!sendMessage(clientConn, response, ec))
            {
                LOGI("Send CaptureResponse fail: %s", ec.message().c_str());
            }
        }
        else
        {
            LOGI("DefaultSrvHdlr Msg type %d unhandled by default.", (int)msg->getMessageType());
        }
    }
};

grpc::Status DiveServiceImpl::StartTrace(grpc::ServerContext *context,
                                         const TraceRequest  *request,
                                         TraceReply          *reply)
{
    GetTraceMgr().TriggerTrace();
    GetTraceMgr().WaitForTraceDone();
    reply->set_trace_file_path(GetTraceMgr().GetTraceFilePath());
    return grpc::Status::OK;
}

grpc::Status DiveServiceImpl::TestConnection(grpc::ServerContext *context,
                                             const TestRequest   *request,
                                             TestReply           *reply)
{
    reply->set_message(request->message() + " received.");
    LOGD("TestConnection request received \n");
    return grpc::Status::OK;
}

grpc::Status DiveServiceImpl::RunCommand(grpc::ServerContext     *context,
                                         const RunCommandRequest *request,
                                         RunCommandReply         *reply)
{
    LOGD("Request command %s", request->command().c_str());
    auto result = ::Dive::RunCommand(request->command());
    if (result.ok())
    {
        reply->set_output(*result);
    }

    return grpc::Status::OK;
}

grpc::Status DiveServiceImpl::GetTraceFileMetaData(grpc::ServerContext       *context,
                                                   const FileMetaDataRequest *request,
                                                   FileMetaDataReply         *response)
{
    std::string target_file = request->name();
    std::cout << "request get metadata for file " << target_file << std::endl;

    response->set_name(target_file);

    if (!std::filesystem::exists(target_file))
    {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "");
    }

    int64_t file_size = std::filesystem::file_size(target_file);
    response->set_size(file_size);

    return grpc::Status::OK;
}

grpc::Status DiveServiceImpl::DownloadFile(grpc::ServerContext             *context,
                                           const DownLoadRequest           *request,
                                           grpc::ServerWriter<FileContent> *writer)
{
    std::string target_file = request->name();
    std::cout << "request to download file " << target_file << std::endl;

    if (!std::filesystem::exists(target_file))
    {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "");
    }
    int64_t file_size = std::filesystem::file_size(target_file);

    FileContent file_content;
    int64_t     total_read = 0;
    int64_t     cur_read = 0;

    std::ifstream fin(target_file, std::ios::binary);
    char          buff[kDownLoadFileChunkSize];
    while (!fin.eof())
    {
        file_content.clear_content();
        cur_read = fin.read(buff, kDownLoadFileChunkSize).gcount();
        total_read += cur_read;
        std::cout << "read " << cur_read << std::endl;
        file_content.set_content(std::string(buff, cur_read));
        writer->Write(file_content);
        if (cur_read != kDownLoadFileChunkSize)
            break;
    }
    std::cout << "Read done, file size " << file_size << ", actually send " << total_read
              << std::endl;
    fin.close();

    if (total_read != file_size)
    {
        std::cout << "file size " << file_size << ", actually send " << total_read << std::endl;
        return grpc::Status(grpc::StatusCode::INTERNAL, "");
    }

    return grpc::Status::OK;
}

void RunServer()
{
    std::string server_address = kUnixAbstractPath;
    std::unique_ptr<TransferInfra::IMessageHandler>
                                              handler = std::make_unique<ServerMessageHandler>();
    std::unique_ptr<TransferInfra::TcpServer> server = std::make_unique<TransferInfra::TcpServer>(
    std::move(handler));

    std::error_code ec;
    if (!server->start(server_address, ec))
    {
        LOGI("Main: Failed to start Socket server");
        return;
    }
    LOGI("Main: Server listening on %s", server_address.c_str());
    server->wait();
    // std::this_thread::sleep_for(std::chrono::seconds(20));
    // We use a Unix (local) domain socket in an abstract namespace rather than an internet domain.
    // It avoids the need to grant INTERNET permission to the target application.
    // Also, no file-based permissions apply since it is in an abstract namespace.
    // DiveServiceImpl service;
    // grpc::EnableDefaultHealthCheckService(true);
    // grpc::ServerBuilder builder;
    // builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

    // builder.RegisterService(&service);
    // std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    // server->Wait();
}

int ServerMain()
{
    RunServer();
    return 0;
}

}  // namespace Dive