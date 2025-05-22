#include "socket_connection.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#    define _WINSOCK_DEPRECATED_NO_WARNINGS
#    define NOMINMAX  // MUST be defined BEFORE including <windows.h> or any other Windows header
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    pragma comment(lib, "Ws2_32.lib")  // For MSVC
typedef SSIZE_T ssize_t;
#else
#    include <fcntl.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <poll.h>
#    include <sys/socket.h>
#    include <sys/stat.h>
#    include <sys/un.h>
#    include <unistd.h>
#    ifndef __ANDROID__
#        include <sys/sendfile.h>
#    endif
#    include <cerrno>
#endif

namespace TransferInfra
{

NetworkInitializer::NetworkInitializer()
{
#ifdef _WIN32
    WSADATA wd;
    int     res = WSAStartup(MAKEWORD(2, 2), &wd);
    if (res == 0)
    {
        initialized_ = true;
        // Register WSACleanup to be called automatically on program exit
        std::atexit([]() { WSACleanup(); });
    }
    else
    {
        initialized_ = false;
        // It's good practice to log or handle the error properly
        std::cerr << "NetworkInitializer: WSAStartup failed with error: " << res << std::endl;
        // Optionally throw an exception if initialization failure is critical
        // throw std::runtime_error("WSAStartup failed: " + std::to_string(res));
    }
#else
    initialized_ = true;
#endif
}

const NetworkInitializer& NetworkInitializer::instance()
{
    // The static local variable 'singleton' is initialized only once,
    // and its construction is thread-safe.
    static NetworkInitializer singleton;
    return singleton;
}

bool NetworkInitializer::isInitialized() const
{
    return initialized_;
}

SocketConnection::SocketConnection() :
    socket_(INVALID_SOCK_VALUE),
    is_listening_(false)
{
    if (!NetworkInitializer::instance().isInitialized())
    {  // Check static instance
        last_error_msg_ = "CRITICAL: Network subsystem failed to initialize.";
    }
    else
    {
        std::cout << "NetworkInitializer initialized correctly!\n";
    }
}

SocketConnection::SocketConnection(SocketType accepted_sock) :
    socket_(accepted_sock),
    is_listening_(false)
{
    // Assumes network_init_ensure_ has run.
}

SocketConnection::~SocketConnection()
{
    close();
}

SocketConnection::SocketConnection(SocketConnection&& other) noexcept :
    socket_(other.socket_),
    is_listening_(other.is_listening_),
    last_error_msg_(std::move(other.last_error_msg_))
{
    other.socket_ = INVALID_SOCK_VALUE;
    other.is_listening_ = false;
}

SocketConnection& SocketConnection::operator=(SocketConnection&& other) noexcept
{
    if (this != &other)
    {
        close();
        socket_ = other.socket_;
        is_listening_ = other.is_listening_;
        last_error_msg_ = std::move(other.last_error_msg_);
        other.socket_ = INVALID_SOCK_VALUE;
        other.is_listening_ = false;
    }
    return *this;
}

void SocketConnection::setError(const std::error_code& ec, const std::string& context)
{
    if (ec)
        last_error_msg_ = (!context.empty() ? context + ": " : "") + ec.message() +
                          " (OS:" + std::to_string(ec.value()) + ")";
    else
        last_error_msg_.clear();
}

void SocketConnection::setPlatformError(const std::string& context)
{
    int err = 0;
#ifdef _WIN32
    err = WSAGetLastError();
#else
    err = errno;
#endif
    setError(std::error_code(err, std::system_category()), context);
}

bool SocketConnection::bindAndListenOnUnixDomain(const std::string& server_address,
                                                 std::error_code&   ec)
{
#ifdef _WIN32
    setPlatformError("Trying to run the server on Windows client! Server must be run on Android "
                     "device.");
    return false;
#else
    ec.clear();
    last_error_msg_.clear();
    if (socket_ != INVALID_SOCK_VALUE)
    {
        close();
    }
    socket_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCK_VALUE)
    {
        setPlatformError("Socket creation failed");
        return false;
    }

    sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    // first char is '\0'
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, server_address.c_str(), server_address.size() + 1);

    int result = ::bind(socket_,
                        (sockaddr*)&addr,
                        (socklen_t)(offsetof(sockaddr_un, sun_path) + 1 + server_address.size()));
    if (result < 0)
    {
        setPlatformError("Bind failed");
        close();
        return false;
    }

    if (::listen(socket_, SOMAXCONN) < 0)
    {
        setPlatformError("Listen failed");
        ec = std::make_error_code(std::errc::io_error);
        close();
        return false;
    }
    is_listening_ = true;
    last_error_msg_.clear();
    return true;
#endif
}

bool SocketConnection::connect(const std::string& host,
                               const std::string& port,
                               std::error_code&   ec)
{
    ec.clear();
    last_error_msg_.clear();
    if (socket_ != INVALID_SOCK_VALUE)
        close();
    addrinfo hints = {}, *si = nullptr, *p = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int res = getaddrinfo(host.c_str(), port.c_str(), &hints, &si);
    if (res != 0)
    {
        last_error_msg_ = "getaddrinfo: " + std::string(gai_strerror(res));
        ec = std::make_error_code(std::errc::network_unreachable);
        if (si)
            freeaddrinfo(si);
        return false;
    }
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> si_guard(si, freeaddrinfo);
    std::string                                        most_recent_err;
    for (p = si; p != nullptr; p = p->ai_next)
    {
        socket_ = (SocketType)::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (socket_ == INVALID_SOCK_VALUE)
        {
            setPlatformError("socket create");
            most_recent_err = last_error_msg_;
            continue;
        }
        if (::connect(socket_, p->ai_addr, (socklen_t)p->ai_addrlen) == -1)
        {
            setPlatformError("connect attempt");
            most_recent_err = last_error_msg_;
            close();
            continue;
        }
        break;
    }
    if (socket_ == INVALID_SOCK_VALUE)
    {
        last_error_msg_ = most_recent_err.empty() ? "connect all fail" : most_recent_err;
        ec = std::make_error_code(std::errc::connection_refused);
        return false;
    }
    is_listening_ = false;
    last_error_msg_.clear();
    return true;
}

size_t SocketConnection::send(const uint8_t* data, size_t size, std::error_code& ec)
{
    ec.clear();
    last_error_msg_.clear();
    if (socket_ == INVALID_SOCK_VALUE || is_listening_)
    {
        ec = std::make_error_code(std::errc::not_connected);
        setError(ec);
        return 0;
    }
    if (size == 0)
        return 0;
    size_t total = 0;
    while (total < size)
    {
        ssize_t sent;
#ifdef _WIN32
        sent = ::send(static_cast<SOCKET>(socket_),
                      (const char*)data + total,
                      (int)(size - total),
                      0);
#else
        sent = ::send(socket_, data + total, size - total, MSG_NOSIGNAL);
#endif
        if (sent == -1)
        {
            setPlatformError("send op");
            int e = 0;
#ifdef _WIN32
            e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK)
                ec = std::make_error_code(std::errc::operation_would_block);
            else if (e == WSAECONNRESET || e == WSAECONNABORTED || e == WSAESHUTDOWN)
            {
                ec = std::make_error_code(std::errc::connection_reset);
                close();
            }
            else
                ec = std::make_error_code(std::errc::io_error);
#else
            e = errno;
            if (e == EAGAIN || e == EWOULDBLOCK)
                ec = std::make_error_code(std::errc::operation_would_block);
            else if (e == EPIPE || e == ECONNRESET)
            {
                ec = std::make_error_code(std::errc::connection_reset);
                close();
            }
            else
                ec = std::make_error_code(std::errc::io_error);
#endif
            if (!ec)
                ec = std::make_error_code(std::errc::io_error);
            return total;
        }
        if (sent == 0)
        {
            if (size > total)
            {
                ec = std::make_error_code(std::errc::connection_aborted);
                setError(ec, "send 0 unexpected");
                close();
            }
            return total;
        }
        total += (size_t)sent;
    }
    return total;
}

size_t SocketConnection::recv(uint8_t* data, size_t size, std::error_code& ec)
{
    ec.clear();
    last_error_msg_.clear();
    if (socket_ == INVALID_SOCK_VALUE || is_listening_)
    {
        ec = std::make_error_code(std::errc::not_connected);
        setError(ec);
        return 0;
    }
    if (size == 0)
        return 0;
    ssize_t recvd;
#ifdef _WIN32
    recvd = ::recv(static_cast<SOCKET>(socket_), (char*)data, (int)size, 0);
#else
    recvd = ::recv(socket_, data, size, 0);
#endif
    if (recvd == -1)
    {
        setPlatformError("recv op");
        int e = 0;
#ifdef _WIN32
        e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK)
            ec = std::make_error_code(std::errc::operation_would_block);
        else if (e == WSAECONNRESET || e == WSAECONNABORTED || e == WSAESHUTDOWN)
        {
            ec = std::make_error_code(std::errc::connection_reset);
            close();
        }
        else
            ec = std::make_error_code(std::errc::io_error);
#else
        e = errno;
        if (e == EAGAIN || e == EWOULDBLOCK)
            ec = std::make_error_code(std::errc::operation_would_block);
        else if (e == ECONNRESET)
        {
            ec = std::make_error_code(std::errc::connection_reset);
            close();
        }
        else
            ec = std::make_error_code(std::errc::io_error);
#endif
        if (!ec)
            ec = std::make_error_code(std::errc::io_error);
        return 0;
    }
    if (recvd == 0)
    {
        ec = std::make_error_code(std::errc::connection_aborted);
        setError(ec, "peer close in recv");
        return 0;
    }
    return (size_t)recvd;
}

std::unique_ptr<SocketConnection> SocketConnection::accept(int timeoutMs, std::error_code& ec)
{
#ifdef _WIN32
    setPlatformError(
    "Trying to execute a server method on Windows client. SocketConnection::accept() method  must "
    "be run on server (Android device).");
    return nullptr;
#else
    ec.clear();
    last_error_msg_.clear();
    if (socket_ == INVALID_SOCK_VALUE || !is_listening_)
    {
        ec = std::make_error_code(std::errc::operation_not_permitted);
        setError(ec);
        return nullptr;
    }
    pollfd pfd;
    pfd.fd = socket_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int r = poll(&pfd, 1, timeoutMs);
    if (r < 0)
    {
        setPlatformError("poll accept");
        ec = std::make_error_code(std::errc::io_error);
        return nullptr;
    }
    if (r == 0)
    {
        ec = std::make_error_code(std::errc::timed_out);
        setError(ec, "accept timeout");
        return nullptr;
    }
    if (!(pfd.revents & POLLIN))
    {
        ec = std::make_error_code(std::errc::io_error);
        setError(ec, "poll err evt accept");
        return nullptr;
    }

    SocketType new_s = (SocketType)::accept(socket_, nullptr, nullptr);
    if (new_s == INVALID_SOCK_VALUE)
    {
        setPlatformError("accept op");
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            ec = std::make_error_code(std::errc::operation_would_block);
        if (!ec)
            ec = std::make_error_code(std::errc::io_error);
        return nullptr;
    }
    return std::make_unique<SocketConnection>(new_s);  // New connection is not listening
#endif
}

bool SocketConnection::sendString(const std::string& s, std::error_code& ec)
{
    const auto len = s.length() + 1;  // include null terminator
    return send(reinterpret_cast<const uint8_t*>(s.c_str()), len, ec) == len;
}
bool SocketConnection::readString(std::string& s, std::error_code& ec)
{
    char c = 0;
    s.clear();
    ec.clear();
    while (true)
    {
        size_t recvd = recv(reinterpret_cast<uint8_t*>(&c), 1, ec);
        if (ec || recvd != 1)
        {
            if (!ec && recvd == 0)
                ec = std::make_error_code(std::errc::connection_aborted);
            s.clear();
            return false;
        }
        if (c == 0)
            return true;
        s.push_back(c);
    }
}

bool SocketConnection::sendFile(const std::string& fp, std::error_code& ec)
{
    ec.clear();
    last_error_msg_.clear();
    std::ifstream f(fp, std::ios::binary | std::ios::ate);
    if (!f)
    {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        setError(ec, "sendFile open " + fp);
        return false;
    }
    std::streamsize sz = f.tellg();
    if (sz < 0)
    {
        ec = std::make_error_code(std::errc::io_error);
        setError(ec, "sendFile size " + fp);
        f.close();
        return false;
    }
    f.seekg(0);
    const size_t      CS = 4096;
    std::vector<char> b(CS);
    std::streamsize   TS = 0;
    while (TS < sz)
    {
        std::streamsize TR = std::min((std::streamsize)CS, sz - TS);
        if (!f.read(b.data(), TR))
        {
            ec = std::make_error_code(std::errc::io_error);
            setError(ec, "sendFile read " + fp);
            f.close();
            return false;
        }
        size_t actual_read = (size_t)f.gcount();
        if (actual_read == 0 && TR > 0)
        {
            ec = std::make_error_code(std::errc::io_error);
            setError(ec, "sendFile 0 read " + fp);
            f.close();
            return false;
        }
        size_t sent_chunk = this->send((uint8_t*)b.data(), actual_read, ec);  // Call member send
        if (ec || sent_chunk != actual_read)
        {
            if (!ec)
                ec = std::make_error_code(std::errc::io_error);
            setError(ec, "sendFile send chunk " + fp);
            f.close();
            return false;
        }
        TS += actual_read;
    }
    f.close();
    return true;
}
bool SocketConnection::receiveFile(const std::string& fp, size_t file_sz, std::error_code& ec)
{
    ec.clear();
    last_error_msg_.clear();
    std::ofstream f(fp, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        ec = std::make_error_code(std::errc::permission_denied);
        setError(ec, "receiveFile open " + fp);
        return false;
    }
    const size_t         CS = 4096;
    std::vector<uint8_t> b(CS);
    size_t               total_recvd = 0;  // Use std::vector<uint8_t> for recv
    while (total_recvd < file_sz)
    {
        size_t to_recv = std::min(CS, file_sz - total_recvd);
        size_t recvd_iter = this->recv(b.data(), to_recv, ec);  // Call member recv
        if (ec || recvd_iter == 0)
        {
            if (!ec && recvd_iter == 0 && (file_sz - total_recvd > 0))
                ec = std::make_error_code(std::errc::connection_aborted);
            setError(ec, "receiveFile recv " + fp);
            f.close();
            return false;
        }
        if (!f.write((char*)b.data(), recvd_iter))
        {
            ec = std::make_error_code(std::errc::io_error);
            setError(ec, "receiveFile write " + fp);
            f.close();
            return false;
        }
        total_recvd += recvd_iter;
    }
    f.close();
    return true;
}

void SocketConnection::close()
{
    if (socket_ != INVALID_SOCK_VALUE)
    {
#ifdef _WIN32
        ::shutdown(static_cast<SOCKET>(socket_), SD_BOTH);
        ::closesocket(static_cast<SOCKET>(socket_));
#else
        ::shutdown(socket_, SHUT_RDWR);
        ::close(socket_);
#endif
        socket_ = INVALID_SOCK_VALUE;
        is_listening_ = false;
    }
}
bool SocketConnection::isOpen() const
{
    return socket_ != INVALID_SOCK_VALUE;
}
std::string SocketConnection::getLastErrorMsg()
{
    std::string t = last_error_msg_;
    return t.empty() ? "No error" : t;
}

}  // namespace TransferInfra