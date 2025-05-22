#pragma once

#include <atomic>  // For NetworkInitializer's static flag
#include <memory>
#include <mutex>  // For NetworkInitializer's static mutex
#include <string>
#include <system_error>
#include <vector>

// Platform specific type definitions
#ifdef _WIN32
typedef uintptr_t SocketType;
const SocketType  INVALID_SOCK_VALUE = ~static_cast<SocketType>(0);
#else
typedef int      SocketType;
const SocketType INVALID_SOCK_VALUE = -1;
#endif

namespace TransferInfra
{

// RAII wrapper for platform network initialization (e.g., Winsock WSAStartup/WSACleanup)
class NetworkInitializer
{
public:
    NetworkInitializer();
    static const NetworkInitializer& instance();
    bool                             isInitialized() const;

private:
    bool initialized_ = false;

    // Prevent copying and assignment
    NetworkInitializer(const NetworkInitializer&) = delete;
    NetworkInitializer& operator=(const NetworkInitializer&) = delete;
};

class SocketConnection
{  // No longer inherits IConnection
private:
    SocketType                socket_ = INVALID_SOCK_VALUE;
    std::string               last_error_msg_;
    bool                      is_listening_ = false;
    static NetworkInitializer network_init_ensure_;  // Ensures global_initializer_ is constructed

    void setError(const std::error_code& ec, const std::string& context = "");
    void setPlatformError(const std::string& context = "");

public:
    SocketConnection();  // Default constructor
    ~SocketConnection();

    // Constructor for an already accepted socket (used by accept method)
    explicit SocketConnection(SocketType accepted_sock);

    SocketConnection(const SocketConnection&) = delete;
    SocketConnection& operator=(const SocketConnection&) = delete;
    SocketConnection(SocketConnection&& other) noexcept;
    SocketConnection& operator=(SocketConnection&& other) noexcept;

    // Server-side methods
    bool bindAndListenOnUnixDomain(const std::string& server_address, std::error_code& ec);
    std::unique_ptr<SocketConnection> accept(int              timeoutMs,
                                             std::error_code& ec);  // Returns concrete type

    // Client-side method
    bool connect(const std::string& host, const std::string& port, std::error_code& ec);

    // Data transfer methods
    size_t send(const uint8_t* data, size_t size, std::error_code& ec);
    size_t recv(uint8_t* data, size_t size, std::error_code& ec);

    bool sendString(const std::string& s, std::error_code& ec);
    bool readString(std::string& s, std::error_code& ec);

    bool sendFile(const std::string& file_path, std::error_code& ec);
    bool receiveFile(const std::string& file_path, size_t file_size, std::error_code& ec);

    void        close();
    bool        isOpen() const;
    std::string getLastErrorMsg();
};

}  // namespace TransferInfra