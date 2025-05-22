#pragma once

#include <algorithm>  // For std::min
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>
#include "log.h"
#include "serializable.h"
#include "socket_connection.h"  // Now depends on concrete SocketConnection for helpers

// Platform specific includes for byte order functions (htonl, ntohl)
#ifdef _WIN32
#    include <winsock2.h>  // For htonl, ntohl. Must be included before windows.h if both are used.
// For 64-bit byte swap, Windows SDK 10.0.10240.0 and later provide _byteswap_uint64
#    include <intrin.h>  // For _byteswap_uint64
#else
#    include <netinet/in.h>              // For htonl, ntohl
#    if __BYTE_ORDER == __LITTLE_ENDIAN  // Specific check for bswap on POSIX
#        include <byteswap.h>            // For bswap_64
#    endif
#endif

namespace TransferInfra
{

// --- MessageType Enum ---
enum class MessageType : uint8_t
{
    HANDSHAKE = 1,
    TRIGGER_CAPTURE = 2,
    TRIGGER_CAPTURE_DONE = 3,
    CAPTURE_REQ = 4,
    CAPTURE_RSP = 5,
    STOP_CAPTURE = 6,
    GET_CAPTURE_FILE_REQ = 7,
    GET_CAPTURE_FILE_RSP = 8,
    CAPTURE_CONFIG = 9,
    LAYER_CAPABILITIES = 10,
    CAPTURE_CONFIG_DONE = 11,
    DATA_PACKET = 12,
    HEARTBEAT_PING = 20,
    HEARTBEAT_PONG = 21,
    UNKNOWN = 0xFF
};

// --- HandShakeMessage ---
class HandShakeMessage : public ISerializable
{
public:
    uint32_t                 major_version = 0;
    uint32_t                 minor_version = 0;
    static constexpr uint8_t TYPE_ID = static_cast<uint8_t>(MessageType::HANDSHAKE);

    uint8_t getMessageType() const override { return TYPE_ID; }

    bool serialize(Buffer& dest, std::error_code& ec) const override
    {
        ec.clear();
        dest.clear();
        dest.resize(sizeof(major_version) + sizeof(minor_version));
        uint32_t net_major = htonl(major_version);
        uint32_t net_minor = htonl(minor_version);
        uint8_t* ptr = dest.data();
        std::memcpy(ptr, &net_major, sizeof(net_major));
        ptr += sizeof(net_major);
        std::memcpy(ptr, &net_minor, sizeof(net_minor));
        return true;
    }
    bool deserialize(const Buffer& src_payload, std::error_code& ec) override
    {
        ec.clear();
        if (src_payload.size() < sizeof(major_version) + sizeof(minor_version))
        {
            ec = std::make_error_code(std::errc::message_size);
            return false;
        }
        const uint8_t* ptr = src_payload.data();
        uint32_t       net_major, net_minor;
        std::memcpy(&net_major, ptr, sizeof(net_major));
        major_version = ntohl(net_major);
        ptr += sizeof(net_major);
        std::memcpy(&net_minor, ptr, sizeof(net_minor));
        minor_version = ntohl(net_minor);
        return true;
    }
};

// --- DataPacketMessage (Example custom message) ---
class CaptureRequest : public ISerializable
{
public:
    std::string              data_field;
    static constexpr uint8_t TYPE_ID = static_cast<uint8_t>(MessageType::CAPTURE_REQ);

    uint8_t getMessageType() const override { return TYPE_ID; }

    bool serialize(Buffer& dest, std::error_code& ec) const override
    {
        ec.clear();
        dest.clear();
        uint32_t str_len = static_cast<uint32_t>(data_field.length());
        uint32_t net_str_len = htonl(str_len);

        size_t total_payload_size = sizeof(net_str_len) + str_len;
        dest.resize(total_payload_size);
        uint8_t* ptr = dest.data();

        std::memcpy(ptr, &net_str_len, sizeof(net_str_len));
        ptr += sizeof(net_str_len);
        if (str_len > 0)
        {
            std::memcpy(ptr, data_field.data(), str_len);
        }
        return true;
    }

    bool deserialize(const Buffer& src_payload, std::error_code& ec) override
    {
        ec.clear();
        if (src_payload.size() < sizeof(uint32_t))
        {
            ec = std::make_error_code(std::errc::message_size);
            return false;
        }
        const uint8_t* ptr = src_payload.data();
        const uint8_t* end_ptr = src_payload.data() + src_payload.size();

        uint32_t net_str_len;
        std::memcpy(&net_str_len, ptr, sizeof(net_str_len));
        uint32_t str_len = ntohl(net_str_len);
        ptr += sizeof(net_str_len);

        if (ptr + str_len > end_ptr)
        {
            ec = std::make_error_code(std::errc::message_size);
            return false;
        }
        if (str_len > 0)
        {
            data_field.assign(reinterpret_cast<const char*>(ptr), str_len);
        }
        else
        {
            data_field.clear();
        }
        return true;
    }
};

class CaptureResponse : public ISerializable
{
public:
    std::string              data_field;
    static constexpr uint8_t TYPE_ID = static_cast<uint8_t>(MessageType::CAPTURE_RSP);

    uint8_t getMessageType() const override { return TYPE_ID; }

    bool serialize(Buffer& dest, std::error_code& ec) const override
    {
        ec.clear();
        dest.clear();
        uint32_t str_len = static_cast<uint32_t>(data_field.length());
        uint32_t net_str_len = htonl(str_len);

        size_t total_payload_size = sizeof(net_str_len) + str_len;
        dest.resize(total_payload_size);
        uint8_t* ptr = dest.data();

        std::memcpy(ptr, &net_str_len, sizeof(net_str_len));
        ptr += sizeof(net_str_len);
        if (str_len > 0)
        {
            std::memcpy(ptr, data_field.data(), str_len);
        }
        return true;
    }

    bool deserialize(const Buffer& src_payload, std::error_code& ec) override
    {
        ec.clear();
        if (src_payload.size() < sizeof(uint32_t))
        {
            ec = std::make_error_code(std::errc::message_size);
            return false;
        }
        const uint8_t* ptr = src_payload.data();
        const uint8_t* end_ptr = src_payload.data() + src_payload.size();

        uint32_t net_str_len;
        std::memcpy(&net_str_len, ptr, sizeof(net_str_len));
        uint32_t str_len = ntohl(net_str_len);
        ptr += sizeof(net_str_len);

        if (ptr + str_len > end_ptr)
        {
            ec = std::make_error_code(std::errc::message_size);
            return false;
        }
        if (str_len > 0)
        {
            data_field.assign(reinterpret_cast<const char*>(ptr), str_len);
        }
        else
        {
            data_field.clear();
        }
        return true;
    }
};

// --- DataPacketMessage (Example custom message) ---
class DataPacketMessage : public ISerializable
{
public:
    std::string              data_field;
    int32_t                  value_field = 0;
    static constexpr uint8_t TYPE_ID = static_cast<uint8_t>(MessageType::DATA_PACKET);

    uint8_t getMessageType() const override { return TYPE_ID; }

    bool serialize(Buffer& dest, std::error_code& ec) const override
    {
        ec.clear();
        dest.clear();
        uint32_t str_len = static_cast<uint32_t>(data_field.length());
        uint32_t net_str_len = htonl(str_len);
        int32_t  net_value = htonl(value_field);

        size_t total_payload_size = sizeof(net_str_len) + str_len + sizeof(net_value);
        dest.resize(total_payload_size);
        uint8_t* ptr = dest.data();

        std::memcpy(ptr, &net_str_len, sizeof(net_str_len));
        ptr += sizeof(net_str_len);
        if (str_len > 0)
        {
            std::memcpy(ptr, data_field.data(), str_len);
            ptr += str_len;
        }
        std::memcpy(ptr, &net_value, sizeof(net_value));
        return true;
    }

    bool deserialize(const Buffer& src_payload, std::error_code& ec) override
    {
        ec.clear();
        if (src_payload.size() < sizeof(uint32_t) + sizeof(int32_t))
        {
            ec = std::make_error_code(std::errc::message_size);
            return false;
        }
        const uint8_t* ptr = src_payload.data();
        const uint8_t* end_ptr = src_payload.data() + src_payload.size();

        uint32_t net_str_len;
        std::memcpy(&net_str_len, ptr, sizeof(net_str_len));
        uint32_t str_len = ntohl(net_str_len);
        ptr += sizeof(net_str_len);

        if (ptr + str_len + sizeof(int32_t) > end_ptr)
        {
            ec = std::make_error_code(std::errc::message_size);
            return false;
        }
        if (str_len > 0)
        {
            data_field.assign(reinterpret_cast<const char*>(ptr), str_len);
        }
        else
        {
            data_field.clear();
        }
        ptr += str_len;

        int32_t net_value;
        std::memcpy(&net_value, ptr, sizeof(net_value));
        value_field = ntohl(net_value);
        return true;
    }
};

// --- HeartbeatMessage (Ping) ---
class HeartbeatMessage : public ISerializable
{
public:
    uint64_t                 timestamp_or_seq = 0;
    static constexpr uint8_t TYPE_ID = static_cast<uint8_t>(MessageType::HEARTBEAT_PING);
    uint8_t                  getMessageType() const override { return TYPE_ID; }

    static uint64_t convert_htonll(uint64_t val)
    {
#if defined(_WIN32) && defined(_byteswap_uint64)
        return _byteswap_uint64(val);
#elif __BYTE_ORDER == __LITTLE_ENDIAN && defined(bswap_64)
        return bswap_64(val);
#else  // Assume big-endian or direct copy if no swap needed/available
       // This might be incorrect on some big-endian systems if they still need a swap for network
       // order. Network byte order is big-endian.
        uint64_t    res;
        char*       dst = (char*)&res;
        const char* src = (const char*)&val;
        if (1 == *(unsigned char*)"\0\1")
        {  // Little endian machine
            for (size_t i = 0; i < sizeof(uint64_t); ++i)
                dst[i] = src[sizeof(uint64_t) - 1 - i];
        }
        else
        {  // Big endian machine
            std::memcpy(&res, &val, sizeof(uint64_t));
        }
        return res;
#endif
    }
    static uint64_t convert_ntohll(uint64_t val) { return convert_htonll(val); }

    bool serialize(Buffer& dest, std::error_code& ec) const override
    {
        ec.clear();
        dest.clear();
        uint64_t net_ts = convert_htonll(timestamp_or_seq);
        dest.resize(sizeof(net_ts));
        std::memcpy(dest.data(), &net_ts, sizeof(net_ts));
        return true;
    }
    bool deserialize(const Buffer& src_payload, std::error_code& ec) override
    {
        ec.clear();
        if (src_payload.size() < sizeof(timestamp_or_seq))
        {
            ec = std::make_error_code(std::errc::message_size);
            return false;
        }
        uint64_t net_ts;
        std::memcpy(&net_ts, src_payload.data(), sizeof(net_ts));
        timestamp_or_seq = convert_ntohll(net_ts);
        return true;
    }
};

// --- HeartbeatResponseMessage (Pong) ---
class HeartbeatResponseMessage : public ISerializable
{
public:
    uint64_t                 original_timestamp_or_seq = 0;
    static constexpr uint8_t TYPE_ID = static_cast<uint8_t>(MessageType::HEARTBEAT_PONG);
    uint8_t                  getMessageType() const override { return TYPE_ID; }

    bool serialize(Buffer& dest, std::error_code& ec) const override
    {
        ec.clear();
        dest.clear();
        uint64_t net_ts = HeartbeatMessage::convert_htonll(original_timestamp_or_seq);
        dest.resize(sizeof(net_ts));
        std::memcpy(dest.data(), &net_ts, sizeof(net_ts));
        return true;
    }
    bool deserialize(const Buffer& src_payload, std::error_code& ec) override
    {
        ec.clear();
        if (src_payload.size() < sizeof(original_timestamp_or_seq))
        {
            LOGI("ecapia error");
            ec = std::make_error_code(std::errc::message_size);
            return false;
        }
        uint64_t net_ts;
        std::memcpy(&net_ts, src_payload.data(), sizeof(net_ts));
        original_timestamp_or_seq = HeartbeatMessage::convert_ntohll(net_ts);
        return true;
    }
};

// --- Message Helper Functions (TLV Framing) ---
inline bool readFull(SocketConnection* conn, uint8_t* buffer, size_t size, std::error_code& ec)
{
    if (!conn)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    ec.clear();
    size_t totalReceived = 0;
    while (totalReceived < size)
    {
        size_t received = conn->recv(buffer + totalReceived, size - totalReceived, ec);
        if (ec || received == 0)
        {
            if (!ec && received == 0)
                ec = std::make_error_code(std::errc::connection_aborted);
            return false;
        }
        totalReceived += received;
    }
    return true;
}

inline bool writeFull(SocketConnection* conn,
                      const uint8_t*    buffer,
                      size_t            size,
                      std::error_code&  ec)
{
    if (!conn)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    ec.clear();
    size_t totalSent = 0;
    while (totalSent < size)
    {
        size_t sent = conn->send(buffer + totalSent, size - totalSent, ec);
        if (ec || (sent == 0 && (size - totalSent > 0)))
        {
            if (!ec && sent == 0)
                ec = std::make_error_code(std::errc::connection_reset);
            return false;
        }
        totalSent += sent;
    }
    return true;
}

inline std::unique_ptr<ISerializable> readMessage(SocketConnection* conn, std::error_code& ec)
{
    if (!conn)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return nullptr;
    }
    ec.clear();
    uint8_t      type_byte;
    uint32_t     net_length;
    const size_t header_size = sizeof(type_byte) + sizeof(net_length);
    uint8_t      header_buffer[header_size];

    if (!readFull(conn, header_buffer, header_size, ec))
    {
        return nullptr;
    }

    std::memcpy(&type_byte, header_buffer, sizeof(type_byte));
    std::memcpy(&net_length, header_buffer + sizeof(type_byte), sizeof(net_length));
    uint32_t payload_length = ntohl(net_length);

    const uint32_t MAX_PAYLOAD_SIZE = 16 * 1024 * 1024;  // 16MB limit
    if (payload_length > MAX_PAYLOAD_SIZE)
    {
        ec = std::make_error_code(std::errc::value_too_large);
        std::cerr << "readMessage: Payload size " << payload_length << " exceeds limit."
                  << std::endl;
        conn->close();
        return nullptr;
    }
    Buffer payload_buffer(payload_length);
    if (payload_length > 0)
    {
        if (!readFull(conn, payload_buffer.data(), payload_length, ec))
        {
            LOGI("readMessage: Failed to read payload of size %d. Error: %s",
                 payload_length,
                 ec.message().c_str());
            return nullptr;
        }
    }

    std::unique_ptr<ISerializable> message = nullptr;
    switch (static_cast<MessageType>(type_byte))
    {
    case MessageType::HANDSHAKE: message = std::make_unique<HandShakeMessage>(); break;
    case MessageType::DATA_PACKET: message = std::make_unique<DataPacketMessage>(); break;
    case MessageType::HEARTBEAT_PING: message = std::make_unique<HeartbeatMessage>(); break;
    case MessageType::HEARTBEAT_PONG: message = std::make_unique<HeartbeatResponseMessage>(); break;
    case MessageType::CAPTURE_REQ: message = std::make_unique<CaptureRequest>(); break;
    case MessageType::CAPTURE_RSP: message = std::make_unique<CaptureResponse>(); break;
    default:
        ec = std::make_error_code(std::errc::illegal_byte_sequence);
        std::cerr << "readMessage: Unknown message type: " << static_cast<int>(type_byte)
                  << std::endl;
        conn->close();
        return nullptr;
    }
    if (message && !message->deserialize(payload_buffer, ec))
    {
        std::cerr << "readMessage: Deserialize failed for type " << static_cast<int>(type_byte)
                  << ": " << ec.message() << std::endl;
        conn->close();
        return nullptr;
    }
    return message;
}

inline bool sendMessage(SocketConnection* conn, const ISerializable& message, std::error_code& ec)
{
    if (!conn)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    ec.clear();
    Buffer payload_buffer;
    if (!message.serialize(payload_buffer, ec))
    {
        std::cerr << "sendMessage: Failed to serialize message type "
                  << static_cast<int>(message.getMessageType()) << ". Error: " << ec.message()
                  << std::endl;
        return false;
    }
    const uint32_t MAX_PAYLOAD_SIZE = 16 * 1024 * 1024;
    if (payload_buffer.size() > MAX_PAYLOAD_SIZE)
    {
        ec = std::make_error_code(std::errc::value_too_large);
        std::cerr << "sendMessage: Payload size " << payload_buffer.size() << " for type "
                  << static_cast<int>(message.getMessageType()) << " exceeds limit." << std::endl;
        return false;
    }
    uint8_t      type_byte = message.getMessageType();
    uint32_t     network_payload_length = htonl(static_cast<uint32_t>(payload_buffer.size()));
    const size_t header_size = sizeof(type_byte) + sizeof(network_payload_length);
    uint8_t      header_buffer[header_size];
    std::memcpy(header_buffer, &type_byte, sizeof(type_byte));
    std::memcpy(header_buffer + sizeof(type_byte),
                &network_payload_length,
                sizeof(network_payload_length));
    if (!writeFull(conn, header_buffer, header_size, ec))
    {
        std::cerr << "sendMessage: Failed to send header for type "
                  << static_cast<int>(message.getMessageType()) << ". Error: " << ec.message()
                  << std::endl;
        return false;
    }
    if (!payload_buffer.empty())
    {
        if (!writeFull(conn, payload_buffer.data(), payload_buffer.size(), ec))
        {
            std::cerr << "sendMessage: Failed to send payload for type "
                      << static_cast<int>(message.getMessageType()) << ". Error: " << ec.message()
                      << std::endl;
            return false;
        }
    }
    return true;
}
}  // namespace TransferInfra