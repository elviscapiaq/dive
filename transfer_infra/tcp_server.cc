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
        std::cout << "Error injecting handler factory. Assigning default.\n";
        this->handler_factory_ = std::make_unique<MyDefaultMessageHandler>();
    }
    std::cout << "TcpServer: Initialized with injected handler factory." << std::endl;
}

TcpServer::~TcpServer()
{
    std::cout << "TcpServer: Destructor. Ensuring server stop." << std::endl;
    stop();
}

void TcpServer::acceptAndHandleClientLoop()
{
    auto server_thread_id = std::this_thread::get_id();
    std::cout << "Server: Accept & Handle Thread (" << server_thread_id << ") started."
              << std::endl;

    IMessageHandler* current_client_handler = nullptr;

    while (running_.load())
    {
        LOGI("Running loop at acceptAndHandleClientLoop");
        std::error_code ec;
        // --- Part 1: Accept a single client ---
        if (!client_connected_.load())
        {  // Only accept if no client is currently connected
            LOGI("No client connected");
            std::cout << "Server Thread (" << server_thread_id
                      << "): Listening for a new client connection..." << std::endl;
            if (!listen_socket_ || !listen_socket_->isOpen())
            {
                if (running_.load())
                {
                    std::lock_guard<std::mutex> lock(server_mutex_);
                    last_error_ = "Listening socket is not open.";
                    std::cerr << "Server Thread (" << server_thread_id << "): " << last_error_
                              << " Stopping." << std::endl;
                    running_.store(false);  // Critical error
                }
                break;  // Exit loop
            }

            // Blocking accept (or with timeout to check running_ flag)
            auto accepted_conn = listen_socket_->accept(ec);  // 2-second timeout

            if (accepted_conn)
            {
                LOGI("New client accepted");
                std::lock_guard<std::mutex> lock(server_mutex_);  // Protect client_connection_
                client_connection_ = std::move(accepted_conn);
                client_connected_.store(true);
                current_client_handler = handler_factory_.get();
                std::cout << "Server Thread (" << server_thread_id
                          << "): Accepted new client. Handler created." << std::endl;
                if (current_client_handler)
                    current_client_handler->onConnect();
            }
            else if (ec && ec != std::errc::timed_out)
            {
                LOGI("Errro accepting new client");
                std::lock_guard<std::mutex> lock(server_mutex_);
                last_error_ = "Accept error: " + ec.message();
                std::cerr << "Server Thread (" << server_thread_id << "): " << last_error_
                          << std::endl;
                if (!listen_socket_->isOpen() || ec == std::errc::bad_file_descriptor)
                {
                    running_.store(false);  // Critical error with listen socket
                    break;
                }
                // For other errors, might just continue trying to accept
            }
            else if (ec == std::errc::timed_out)
            {
                LOGI("Accept timed out: continue waiting for new client connection");
                // Timeout is fine, loop again if running_ is true
                continue;
            }
        }  // End of accept block

        // --- Part 2: Handle the connected client ---
        if (client_connected_.load() && client_connection_ && client_connection_->isOpen())
        {
            // Try to read a message (this part needs to be non-blocking or have a short timeout
            // to allow the outer loop to check running_ and the client_connected_ status if we want
            // this single thread to also re-accept quickly after a disconnect).
            // For true synchronous handling within this loop, readMessage would block.
            // Let's assume readMessage has some internal timeout or we add select/poll here.
            // For simplicity in this example, we'll use the blocking readMessage.
            // If readMessage blocks indefinitely, a new client cannot connect until this one
            // disconnects.

            // To make the accept loop more responsive if this part blocks for too long on read,
            // we'd ideally want SocketConnection::recv to have a timeout.
            // Our current readMessage helper uses blocking recv.
            // A quick check with a short timeout on poll could be added before readMessage.
            // For now, we use a simple blocking read.

            auto message = TransferInfra::readMessage(client_connection_.get(), ec);

            if (message)
            {
                if (current_client_handler)
                {
                    LOGI("handleMessage");
                    current_client_handler->handleMessage(std::move(message),
                                                          client_connection_.get());
                }
            }
            else
            {  // readMessage failed or client disconnected cleanly
                LOGI("readMessage failed or client disconnected cleanly");
                if (ec)
                {
                    if (ec != std::errc::connection_aborted)
                    {  // Don't spam for clean disconnects
                        std::lock_guard<std::mutex> lock(server_mutex_);
                        last_error_ = "Client handling error (read): " + ec.message();
                        std::cerr << "Server Thread (" << server_thread_id << "): " << last_error_
                                  << std::endl;
                    }
                }
                LOGI("Client disconnected or read error. Preparing to accept new client.");
                if (current_client_handler)
                    current_client_handler->onDisconnect();
                {
                    std::lock_guard<std::mutex> lock(server_mutex_);  // Protect client_connection_
                    if (client_connection_)
                        client_connection_->close();
                    client_connection_.reset();
                }
                client_connected_.store(false);
                // Loop will now go back to accepting
            }
        }
        else if (client_connected_.load() && (!client_connection_ || !client_connection_->isOpen()))
        {
            // If we thought a client was connected, but the socket is now bad
            std::cout << "Server Thread (" << server_thread_id
                      << "): Previously connected client socket is no longer valid. Resetting."
                      << std::endl;
            if (current_client_handler)
                current_client_handler->onDisconnect();
            {
                std::lock_guard<std::mutex> lock(server_mutex_);
                if (client_connection_)
                    client_connection_->close();  // Ensure it's closed
                client_connection_.reset();
            }
            client_connected_.store(false);
        }

        // If running_ is false, the loop will exit.
        // Add a small sleep if no client is connected to prevent busy-looping on accept timeout
        if (!client_connected_.load() && running_.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Small pause
        }

    }  // end while(running_)

    // Cleanup if loop exits
    LOGI("Loop exited at acceptAndHandleClientLoop");
    std::cout << "Server Thread (" << server_thread_id << "): Loop exited. Cleaning up..."
              << std::endl;
    if (client_connected_.load())
    {
        if (current_client_handler)
            current_client_handler->onDisconnect();
        {
            std::lock_guard<std::mutex> lock(server_mutex_);
            if (client_connection_)
                client_connection_->close();
            client_connection_.reset();
        }
        client_connected_.store(false, std::memory_order_relaxed);
    }
    if (listen_socket_ && listen_socket_->isOpen())
    {
        listen_socket_->close();
    }
    LOGI("Server: Accept & Handle Thread exiting");
    std::cout << "Server: Accept & Handle Thread (" << server_thread_id << ") exiting fully."
              << std::endl;
}

bool TcpServer::start(const std::string& server_address, std::error_code& ec)
{
    std::lock_guard<std::mutex> lock(server_mutex_);
    if (running_.load())
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
    running_ = true;
    LOGI("Server before acceptLoop on %s", server_address.c_str());
    server_thread_ = std::thread(&TcpServer::acceptAndHandleClientLoop, this);
    LOGI("Server after acceptLoop started on %s", server_address.c_str());
    return true;
}

void TcpServer::wait()
{
    while (running_.load())
    {
        LOGI("Main thread waiting");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void TcpServer::stop()
{
    if (!running_.exchange(false))
    {
        LOGI("Server (Single Client Mode): Stop called, but server was not running or "
             "already stopping.");
        if (server_thread_.joinable())
        {  // Attempt to join if it was somehow left
            try
            {
                LOGI("Server: Server thread joined successfully.");
                server_thread_.join();
            }
            catch (const std::system_error& e)
            {
                LOGI("Server: System error while joining server thread: %s", e.what());
            }
        }
        return;
    }
    LOGI("Server (Single Client Mode): Stop sequence initiated...");

    // 1. Signal the server_thread_ loop to stop (done by setting running_ to false)
    // 2. Close the listening socket to unblock accept() if it's waiting.
    LOGI("Server: Closing listening socket to unblock server thread...");
    if (listen_socket_)
    {
        listen_socket_->close();
    }

    // 3. Close the active client connection (if any) to unblock its handler's readMessage()
    {
        std::lock_guard<std::mutex> lock(server_mutex_);  // Protect client_connection_
        if (client_connection_ && client_connection_->isOpen())
        {
            LOGI("Server: Closing active client connection to signal handler...");
            client_connection_->close();
        }
    }

    // 4. Notify condition variable if server_thread_ is waiting on it (not used in this simple
    // loop)
    stop_cv_.notify_one();  // In case the loop was designed to wait on this

    // 5. Wait for the server_thread_ to finish
    if (server_thread_.joinable())
    {
        try
        {
            server_thread_.join();
            LOGI("Server: Server thread joined successfully.");
        }
        catch (const std::system_error& e)
        {
            LOGI("Server: System error while joining server thread: %s", e.what());
        }
    }
    else
    {
        LOGI("Server: Server thread was not joinable upon stop.");
    }

    // Reset resources
    if (listen_socket_)
        listen_socket_.reset();
    {
        std::lock_guard<std::mutex> lock(server_mutex_);
        if (client_connection_)
            client_connection_.reset();
    }
    client_connected_.store(false);

    LOGI("Server (Single Client Mode): Stopped completely.");
}

bool TcpServer::isRunning() const
{
    return running_.load();
}

bool TcpServer::isClientConnected() const
{
    return client_connected_.load();
}

std::string TcpServer::getLastError() const
{
    std::lock_guard<std::mutex> l(server_mutex_);
    return last_error_;
}

}  // namespace TransferInfra