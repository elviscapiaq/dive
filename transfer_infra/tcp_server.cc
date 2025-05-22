#include "tcp_server.h"
#include <chrono>
#include <iostream>
#include <vector>  // For stop method logic
#include "log.h"

namespace TransferInfra
{

MyDefaultMessageHandler::MyDefaultMessageHandler() {}

void MyDefaultMessageHandler::onConnect()
{
    LOGI("DefaultSrvHdlr (Thread: %d ): MyDefaultMessageHandler onConnect()",
         std::this_thread::get_id());
}

void MyDefaultMessageHandler::handleMessage(std::unique_ptr<ISerializable> msg,
                                            SocketConnection*              clientConn)
{
    auto tid = std::this_thread::get_id();
    LOGI("DefaultSrvHdlr (Thread:%d ): RX msg type %d", tid, (int)msg->getMessageType());
    if (!msg || !clientConn)
    {
        LOGI("Null msg/conn");
        return;
    }

    if (msg->getMessageType() == static_cast<uint8_t>(MessageType::HEARTBEAT_PING))
    {
        auto* ping = dynamic_cast<HeartbeatMessage*>(msg.get());
        if (ping)
        {
            LOGI("HeartbeatMessage: ping");
            HeartbeatResponseMessage pong;
            pong.original_timestamp_or_seq = ping->timestamp_or_seq;
            std::error_code ec_p;
            if (!TransferInfra::sendMessage(clientConn, pong, ec_p))
            {
                LOGI("Send PONG fail: %s", ec_p.message().c_str());
            }
        }
    }
    else if (msg->getMessageType() == static_cast<uint8_t>(MessageType::HANDSHAKE))
    {
        auto* hs_msg = dynamic_cast<HandShakeMessage*>(msg.get());
        if (hs_msg)
        {
            LOGI("HandShakeMessage: handshake");
            HandShakeMessage hs_response_msg;
            hs_response_msg.major_version = hs_msg->major_version;
            hs_response_msg.minor_version = hs_msg->minor_version;
            std::error_code ec;
            if (!TransferInfra::sendMessage(clientConn, hs_response_msg, ec))
            {
                LOGI("Send Handshake fail: %s", ec.message().c_str());
            }
        }
    }
    else
    {
        LOGI("DefaultSrvHdlr Msg type %d unhandled by default.", (int)msg->getMessageType());
    }
}
void MyDefaultMessageHandler::onDisconnect()
{
    LOGI("DefaultSrvHdlr (Thread: %d ): MyDefaultMessageHandler onDisconnect()",
         std::this_thread::get_id());
}

TcpServer::TcpServer() :
    handler_factory_(std::make_unique<MyDefaultMessageHandler>())
{
    std::cout << "TcpServer: Initialized with default handler factory." << std::endl;
}

TcpServer::TcpServer(std::unique_ptr<IMessageHandler> handler_factory) :
    handler_factory_(std::move(handler_factory))
{
    if (!this->handler_factory_)
    {
        this->handler_factory_ = std::make_unique<MyDefaultMessageHandler>();
    }
    std::cout << "TcpServer: Initialized with injected handler factory." << std::endl;
}

TcpServer::~TcpServer()
{
    std::cout << "TcpServer: Destructor. Ensuring server stop." << std::endl;
    stop();
}

void TcpServer::acceptLoop()
{
    auto acc_tid = std::this_thread::get_id();
    std::cout << "Server: Acceptor Thread (" << acc_tid << ") started." << std::endl;
    while (running_.load(std::memory_order_relaxed))
    {
        LOGI("Waiting for a client connection");
        std::error_code                   ec;
        std::unique_ptr<SocketConnection> client_socket_conn = nullptr;
        if (listen_socket_ && listen_socket_->isOpen())
        {
            client_socket_conn = listen_socket_
                                 ->accept(1000, ec);  // accept returns unique_ptr<SocketConnection>
        }
        else
        {
            if (running_.load(std::memory_order_relaxed))
            {
                std::lock_guard<std::mutex> l(server_mutex_);
                last_error_ = "Listen sock not open.";
                std::cerr << "Srv: AccLoop - " << last_error_ << std::endl;
                running_.store(false, std::memory_order_relaxed);
            }
            break;
        }
        if (client_socket_conn)
        {  // Successfully accepted
            LOGI("Server: AccLoop - Accepted client.");
            auto* handler = handler_factory_.get();

            LOGI("Before thread for clientHandlerLoop");
            std::thread client_th(
            [this, conn = std::move(client_socket_conn), h = handler]() mutable {
                this->clientHandlerLoop(std::move(conn), h);
            });
            std::lock_guard<std::mutex> l(server_mutex_);
            client_threads_.push_back(std::move(client_th));
            LOGI("After thread for clientHandlerLoop");
        }
        else if (ec)
        {  // Error during accept
            if (ec != std::errc::timed_out)
            {  // Ignore timeouts, they are expected
                std::lock_guard<std::mutex> l(server_mutex_);
                last_error_ = "Accept err:" + ec.message() + " (" +
                              (listen_socket_ ? listen_socket_->getLastErrorMsg() : "N/A") + ")";
                std::cerr << "Srv: AccLoop - " << last_error_ << std::endl;
                if (!listen_socket_ || !listen_socket_->isOpen() ||
                    ec == std::errc::bad_file_descriptor)
                {
                    std::cerr << "Srv: AccLoop - Critical listen sock err. Stopping." << std::endl;
                    running_.store(false, std::memory_order_relaxed);
                    break;
                }
            }
        }
    }
    std::cout << "Server: Acceptor Thread (" << acc_tid << ") exiting." << std::endl;
}

void TcpServer::clientHandlerLoop(std::unique_ptr<SocketConnection> client_conn,
                                  IMessageHandler*                  handler)
{
    if (!client_conn || !handler)
    {
        std::cerr << "Srv Err (Thread " << std::this_thread::get_id()
                  << "): clientHandlerLoop null conn/hdlr." << std::endl;
        return;
    }
    auto cl_tid = std::this_thread::get_id();
    std::cout << "Server: Client Handler Thread (" << cl_tid << ") started for a client."
              << std::endl;
    handler->onConnect();
    std::error_code ec;
    try
    {
        while (running_.load(std::memory_order_relaxed) && client_conn->isOpen())
        {
            LOGI("clientHandlerLoop");
            auto msg = TransferInfra::readMessage(client_conn.get(), ec);  // Pass SocketConnection*
            if (msg)
            {
                LOGI("handleMessage");
                handler->handleMessage(std::move(msg), client_conn.get());
            }
            else
            {  // readMessage failed or client disconnected
                LOGI("readMessage failed or client disconnected");
                if (ec)
                {
                    if (ec != std::errc::connection_aborted && ec != std::errc::timed_out)
                        LOGI("Srv: ClientHdlr (%d) ReadMsg err: %s (%s)",
                             cl_tid,
                             ec.message().c_str(),
                             client_conn->getLastErrorMsg().c_str());
                    else if (ec == std::errc::connection_aborted)
                        LOGI("Srv: ClientHdlr (%d) Conn closed by peer.", cl_tid);
                }
                else
                    std::cout << "Srv: ClientHdlr (" << cl_tid
                              << ") Conn closed by peer (readMessage null no ec)." << std::endl;
                break;  // Exit loop
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Srv: ClientHdlr (" << cl_tid << ") Exception: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Srv: ClientHdlr (" << cl_tid << ") Unknown exception." << std::endl;
    }

    std::cout << "Srv: ClientHdlr (" << cl_tid << ") Calling onDisconnect." << std::endl;
    handler->onDisconnect();
    if (client_conn && client_conn->isOpen())
    {
        std::cout << "Srv: ClientHdlr (" << cl_tid << ") Closing client conn." << std::endl;
        client_conn->close();
    }
    std::cout << "Server: Client Handler Thread (" << cl_tid << ") exiting." << std::endl;
}

bool TcpServer::start(const std::string& server_address, std::error_code& ec)
{
    std::lock_guard<std::mutex> lock(server_mutex_);
    if (running_.load(std::memory_order_relaxed))
    {
        ec = std::make_error_code(std::errc::device_or_resource_busy);
        last_error_ = "Srv already running.";
        return false;
    }
    last_error_.clear();
    listen_socket_ = std::make_unique<SocketConnection>();  // Directly create SocketConnection
    if (!listen_socket_->bindAndListenOnUnixDomain(server_address, ec))
    {  // Use public bindAndListen
        last_error_ = "Srv listen_socket setup fail: " +
                      (ec ? ec.message() : listen_socket_->getLastErrorMsg());
        listen_socket_.reset();
        return false;
    }
    running_.store(true, std::memory_order_relaxed);
    acceptLoop();
    // accept_thread_ = std::thread(&TcpServer::acceptLoop, this);
    std::cout << "Server: Started on " << server_address << std::endl;
    return true;
}

void TcpServer::stop()
{
    if (!running_.exchange(false, std::memory_order_acq_rel))
    {
        std::cout << "Srv: Stop called, but not running/already stopping." << std::endl;
        if (accept_thread_.joinable())
        {
            try
            {
                accept_thread_.join();
            }
            catch (const std::system_error& e)
            {
                std::cerr << "Srv: Error joining accept_thread during redundant stop: " << e.what()
                          << std::endl;
            }
        }
        std::list<std::thread> tj;
        {
            std::lock_guard<std::mutex> l(server_mutex_);
            tj.swap(client_threads_);
        }
        for (auto& t : tj)
        {
            if (t.joinable())
            {
                try
                {
                    t.join();
                }
                catch (const std::system_error& e)
                {
                    std::cerr << "Srv: Error joining client_thread during redundant stop: "
                              << e.what() << std::endl;
                }
            }
        }
        return;
    }
    std::cout << "Srv: Stop initiated..." << std::endl;
    std::cout << "Srv: Closing listen socket..." << std::endl;
    if (listen_socket_)
        listen_socket_->close();

    std::cout << "Srv: Joining acceptor thread (" << accept_thread_.get_id() << ")..." << std::endl;
    if (accept_thread_.joinable())
    {
        try
        {
            accept_thread_.join();
            std::cout << "Srv: Acceptor joined." << std::endl;
        }
        catch (const std::system_error& e)
        {
            std::cerr << "Srv: Sys err joining acceptor: " << e.what() << std::endl;
        }
    }
    else
    {
        std::cout << "Srv: Acceptor not joinable on stop." << std::endl;
    }

    std::list<std::thread> threads_to_join;
    {
        std::lock_guard<std::mutex> l(server_mutex_);
        threads_to_join.swap(client_threads_);
    }  // Move threads out of list while holding lock

    std::cout << "Srv: Joining " << threads_to_join.size() << " client threads..." << std::endl;
    for (auto& t : threads_to_join)
    {
        if (t.joinable())
        {
            std::cout << "Srv: Joining client thread (" << t.get_id() << ")..." << std::endl;
            try
            {
                t.join();
            }
            catch (const std::system_error& e)
            {
                std::cerr << "Srv: Sys err joining client thread (" << t.get_id()
                          << "): " << e.what() << std::endl;
            }
        }
    }
    // Ensure original list is clear
    std::lock_guard<std::mutex> l(server_mutex_);
    client_threads_.clear();
    std::cout << "Srv: Client threads joined." << std::endl;

    if (listen_socket_)
        listen_socket_.reset();
    std::cout << "Srv: Stopped." << std::endl;
}

bool TcpServer::isRunning() const
{
    return running_.load(std::memory_order_relaxed);
}
std::string TcpServer::getLastError() const
{
    std::lock_guard<std::mutex> l(server_mutex_);
    return last_error_;
}

}  // namespace TransferInfra