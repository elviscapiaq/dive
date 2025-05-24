#pragma once

#include "messages.h"  // For HandShakeMessage, HeartbeatMessage, etc.
#include "serializable.h"
#include "socket_connection.h"  // Direct use of SocketConnection

#include <chrono>
#include <condition_variable>  // For graceful shutdown of keep-alive thread
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

namespace TransferInfra
{

// ClientStatus enum remains useful for TcpClient's internal state
enum class ClientStatusInternalStatus
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    STATUS_ERROR,
    PINGING,
    TRANSFERRING_FILE
};

class TcpClient
{                                                   // No IClientState interface
    std::unique_ptr<SocketConnection> connection_;  // Direct use of SocketConnection

    ClientStatusInternalStatus status_ = ClientStatusInternalStatus::DISCONNECTED;
    std::string                last_error_;
    mutable std::mutex         client_mutex_;

    // kepp alive members
    std::thread          keep_alive_thread_;
    std::atomic<bool>    run_keep_alive_{ false };
    std::chrono::seconds keep_alive_interval_{ 5 };            // Default interval
    int                  keep_alive_ping_timeout_ms_{ 2000 };  // Default ping timeout
    std::mutex keep_alive_mutex_;  // Separate mutex for keep-alive thread condition variable
    std::condition_variable keep_alive_cv_;  // For signaling keep-alive thread to stop

    bool performHandshake(std::error_code& ec);
    void keepAliveLoop();  // Method executed by the keep_alive_thread_

public:
    TcpClient();  // Default constructor, creates SocketConnection internally
    ~TcpClient();

    // Public API methods
    bool connect(const std::string& host, const std::string& port, std::error_code& ec);
    void disconnect();
    bool isConnected() const;
    ClientStatusInternalStatus getStatus() const;  // Returns internal status enum
    std::string                getLastError() const;

    bool                           sendMessage(const ISerializable& message, std::error_code& ec);
    std::unique_ptr<ISerializable> receiveMessage(int timeout_ms, std::error_code& ec);
    bool                           pingServer(int timeout_ms, std::error_code& ec);
    std::string                    startCapture(std::error_code& ec);
    // Downloads a file from the server to a local path.
    // remote_filename: The name/identifier of the file on the server.
    // local_save_path: Where to save the downloaded file locally.
    bool downloadFileFromServer(const std::string& remote_filename,
                                const std::string& local_save_path,
                                std::error_code&   ec);

    // --- Keep-Alive Management ---
    // Starts the internal keep-alive thread.
    // interval_sec: How often to send pings.
    // ping_timeout_ms: Timeout for waiting for a pong response.
    void startKeepAlive(int interval_sec = 5, int ping_timeout_ms = 2000);
    // Stops the internal keep-alive thread.
    void stopKeepAlive();
};

}  // namespace TransferInfra