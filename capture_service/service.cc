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

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"
#include "command_utils.h"
#include "constants.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "log.h"
#include "trace_mgr.h"

namespace Dive
{

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

const int BUFFER_SIZE = 4096;
const int PORT = 19999;
char      buffer[BUFFER_SIZE];
char      file_buffer[BUFFER_SIZE];

void RunSocketServer()
{
    // Create socket file descriptor
    int server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        LOGE("socket failed");
        return;
    }

    // int flags = fcntl(server_socket, F_GETFL, 0);
    // fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);

    // flags = fcntl(server_socket, F_GETFD, 0);
    // fcntl(server_socket, F_SETFD, flags | FD_CLOEXEC);

    std::string server_address = absl::StrFormat("dive_%d", PORT);

    sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    // first char is '\0'
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, server_address.c_str(), server_address.size() + 1);

    // Bind the socket to the specified port
    int result = bind(server_socket,
                      (sockaddr *)&addr,
                      (socklen_t)(offsetof(sockaddr_un, sun_path) + 1 + server_address.size()));
    if (result < 0)
    {
        LOGE("Bind failed");
        close(server_socket);
        return;
    }

    if (listen(server_socket, 3) < 0)
    {
        LOGE("listen failed");
        close(server_socket);
        return;
    }

    LOGI("Server listening on %s\n", server_address.c_str());

    do
    {
        LOGI("Waiting for a connection...");
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket < 0)
        {
            LOGE("Accept failed");
            continue;  // Go back to waiting for connections
        }

        LOGI("Connection accepted\n");

        // 1. Receive filename request
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t valread = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (valread <= 0)
        {
            LOGE("Error reading filename or client disconnected");
            close(client_socket);
            continue;
        }
        buffer[valread] = '\0';
        std::string requested_filename = buffer;
        LOGI("Requested filename: %s", requested_filename.c_str());

        // 2. Construct full path
        std::string full_path = requested_filename;
        LOGI("Attempting to open: %s", full_path.c_str());

        std::ifstream file(full_path, std::ios::binary);
        if (!file.is_open())
        {
            LOGE("Could not open file: %s", full_path.c_str());
            const char *error_msg = "NOT_FOUND";
            send(client_socket, error_msg, strlen(error_msg), 0);
            close(client_socket);
            continue;
        }

        // 4. Send file size
        file.seekg(0, std::ios::end);
        long long file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::string file_size_str = std::to_string(file_size);
        if (send(client_socket, file_size_str.c_str(), file_size_str.length(), 0) !=
            file_size_str.length())
        {
            LOGE("Error sending file size");
            file.close();
            close(client_socket);
            continue;
        }

        // 5. Send file content
        LOGI("Sending file: %s (%lld bytes)", requested_filename.c_str(), file_size);
        long long total_sent = 0, bytes_read = 0;
        while (total_sent < file_size)
        {
            file.read(file_buffer, BUFFER_SIZE);
            bytes_read = file.gcount();
            LOGI("bytes_read = %lld", bytes_read);
            if (send(client_socket, file_buffer, bytes_read, 0) != bytes_read)
            {
                LOGE("Error sending file data");
                goto cleanup;
            }
            total_sent += bytes_read;
            LOGI("Sent %lld / %lld bytes", total_sent, file_size);
        }
    cleanup:
        file.close();
        LOGI("Total sent = %lld", total_sent);
        close(client_socket);
        LOGI("Client socket closed");
    } while (false);

    close(server_socket);
}

void RunServer()
{
    // We use a Unix (local) domain socket in an abstract namespace rather than an internet domain.
    // It avoids the need to grant INTERNET permission to the target application.
    // Also, no file-based permissions apply since it is in an abstract namespace.
    std::string     server_address = absl::StrFormat("unix-abstract:%s", kUnixAbstractPath);
    DiveServiceImpl service;
    grpc::EnableDefaultHealthCheckService(true);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    LOGI("Server listening on %s", server_address.c_str());
    server->Wait();
}

int ServerMain()
{
    // RunServer();
    RunSocketServer();
    return 0;
}

}  // namespace Dive