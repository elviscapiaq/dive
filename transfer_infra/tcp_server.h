#pragma once

// No IServer interface anymore
#include "message_handler.h"    // Kept for decoupling message processing
#include "messages.h"           // For HandShakeMessage, HeartbeatMessage, etc.
#include "serializable.h"       // For message types
#include "socket_connection.h"  // Direct use of SocketConnection

#include <atomic>
#include <condition_variable>  // For signaling during shutdown
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
{
    std::unique_ptr<SocketConnection> listen_socket_;      // For accepting connections
    std::unique_ptr<SocketConnection> client_connection_;  // For the single active client
    std::unique_ptr<IMessageHandler>  handler_factory_;

    std::thread       server_thread_;              // Single thread for accept and client handling
    std::atomic<bool> running_{ false };           // Controls server running state
    std::atomic<bool> client_connected_{ false };  // Tracks if a client is currently connected

    std::string last_error_;
    mutable std::mutex
    server_mutex_;  // Protects shared state like last_error_ and client_connection_
    std::condition_variable stop_cv_;  // For stop() to signal server_thread_

    // Single loop for accepting one client and then handling it.
    void acceptAndHandleClientLoop();

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
    bool        isClientConnected() const;  // New method to check client status
    std::string getLastError() const;
};
}  // namespace TransferInfra