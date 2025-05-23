#pragma once

// No IServer interface anymore
#include "message_handler.h"    // Kept for decoupling message processing
#include "messages.h"           // For HandShakeMessage, HeartbeatMessage, etc.
#include "serializable.h"       // For message types
#include "socket_connection.h"  // Direct use of SocketConnection

#include <atomic>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace TransferInfra
{

// Default message handler if user doesn't provide one.
class MyDefaultMessageHandler : public IMessageHandler
{
public:
    MyDefaultMessageHandler();
    void onConnect() override;
    void handleMessage(std::unique_ptr<ISerializable> message,
                       SocketConnection*              clientConnection) override;
    void onDisconnect() override;
};

class TcpServer
{                                                      // No longer inherits IServer
    std::unique_ptr<SocketConnection> listen_socket_;  // Direct use
    std::unique_ptr<IMessageHandler>  handler_factory_;

    std::list<std::thread> client_threads_;
    std::thread            accept_thread_;
    std::atomic<bool>      running_;
    std::string            last_error_;
    mutable std::mutex     server_mutex_;  // Protects client_threads_ and last_error_

    void acceptLoop();
    void clientHandlerLoop(std::unique_ptr<SocketConnection> client_conn, IMessageHandler* handler);

public:
    // Constructor uses default SocketConnection creation internally and default message handler
    // factory
    TcpServer();
    // Constructor allowing injection of a custom message handler factory
    explicit TcpServer(std::unique_ptr<IMessageHandler> handler_factory);
    ~TcpServer();

    // Public API methods
    bool        start(const std::string& server_address, std::error_code& ec);
    void        wait();
    void        stop();
    bool        isRunning() const;
    std::string getLastError() const;
};
}  // namespace TransferInfra