#pragma once

#include <memory>
#include "serializable.h"       // Adjusted path
#include "socket_connection.h"  // Adjusted path

namespace TransferInfra
{

// Interface for handling received messages on the server side.
// An instance is typically created for each client connection.
class IMessageHandler
{
public:
    virtual ~IMessageHandler() = default;

    // Called by the Client Handler Thread when a new client connection is established
    // and this handler instance is associated with it.
    virtual void onConnect() {}

    // Called by the Client Handler Thread to process a fully received and deserialized message.
    // Takes ownership of the message unique_ptr.
    // The clientConnection can be used to send responses back to this specific client.
    virtual void handleMessage(std::unique_ptr<ISerializable> message,
                               SocketConnection*              clientConnection) = 0;

    // Called by the Client Handler Thread when the connection associated with this handler
    // is being closed (either by client, error, or server shutdown).
    virtual void onDisconnect() {}
};

}  // namespace TransferInfra