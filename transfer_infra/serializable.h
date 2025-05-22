#pragma once

#include <cstdint>
#include <system_error>
#include <vector>

namespace TransferInfra
{

using Buffer = std::vector<uint8_t>;  // Common typedef for byte buffers

class ISerializable
{
public:
    virtual ~ISerializable() = default;

    // Returns the specific type identifier for this message.
    virtual uint8_t getMessageType() const = 0;

    // Serializes the object's payload into the destination buffer.
    // Returns true on success, false on failure (setting ec).
    virtual bool serialize(Buffer& destination_payload, std::error_code& ec) const = 0;

    // Deserializes the object's state from the source buffer (which contains only the payload).
    // Returns true on success, false on failure (setting ec).
    virtual bool deserialize(const Buffer& source_payload, std::error_code& ec) = 0;
};

}  // namespace TransferInfra