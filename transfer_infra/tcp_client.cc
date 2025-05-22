#include "tcp_client.h"
// Messages.h included via TcpClient.h

namespace TransferInfra
{

TcpClient::TcpClient()
{
    std::cout << "TcpClient: Initialized." << std::endl;
}

TcpClient::~TcpClient()
{
    std::cout << "TcpClient: Destructor called." << std::endl;
    stopKeepAlive();  // Ensure keep-alive thread is stopped first
    disconnect();     // Then disconnect and clean up connection
}

bool TcpClient::performHandshake(std::error_code& ec)
{
    // Assumes client_mutex_ is held by caller (connect).
    // Assumes connection_ is valid.
    HandShakeMessage hs_send;
    hs_send.major_version = 3;
    hs_send.minor_version = 3;
    std::cout << "Client: Sending Handshake (Client v" << hs_send.major_version << "."
              << hs_send.minor_version << ")" << std::endl;
    if (!TransferInfra::sendMessage(connection_.get(), hs_send, ec))
    {
        last_error_ = "Handshake send fail: " +
                      (ec ? ec.message() : connection_->getLastErrorMsg());
        return false;
    }
    std::cout << "Client: Waiting for Handshake response..." << std::endl;
    auto response_base = TransferInfra::readMessage(connection_.get(), ec);
    if (!response_base)
    {
        last_error_ = "Handshake recv fail: " +
                      (ec ? ec.message() :
                            (connection_ ? connection_->getLastErrorMsg() : "Unknown read err"));
        if (ec == std::errc::connection_aborted)
            std::cout << "Client: Server closed during handshake recv." << std::endl;
        return false;
    }
    if (response_base->getMessageType() == HandShakeMessage::TYPE_ID)
    {
        auto* hs_recv = dynamic_cast<HandShakeMessage*>(response_base.get());
        if (hs_recv)
        {
            std::cout << "Client: Server Handshake (Server v" << hs_recv->major_version << "."
                      << hs_recv->minor_version << ")" << std::endl;
            if (hs_recv->major_version != hs_send.major_version)
            {
                ec = std::make_error_code(std::errc::protocol_error);
                last_error_ = "Handshake major ver mismatch. Server: v" +
                              std::to_string(hs_recv->major_version) + ", Client: v" +
                              std::to_string(hs_send.major_version);
                return false;
            }
            std::cout << "Client: Handshake versions compatible." << std::endl;
            return true;
        }
    }
    ec = std::make_error_code(std::errc::protocol_error);
    last_error_ = "Unexpected msg type in handshake (Expected: " +
                  std::to_string(HandShakeMessage::TYPE_ID) +
                  ", Got: " + std::to_string(static_cast<int>(response_base->getMessageType())) +
                  ").";
    return false;
}

bool TcpClient::connect(const std::string& host, const std::string& port, std::error_code& ec)
{
    std::lock_guard<std::mutex> lock(client_mutex_);  // Protect status_ and connection_
    if (status_ == ClientStatusInternalStatus::CONNECTED ||
        status_ == ClientStatusInternalStatus::CONNECTING)
    {
        ec = std::make_error_code(std::errc::already_connected);
        last_error_ = "Already connected/connecting.";
        return false;
    }

    stopKeepAlive();  // Stop any existing keep-alive thread before attempting new connection
    if (connection_)
    {
        connection_->close();
        connection_.reset();
    }

    status_ = ClientStatusInternalStatus::CONNECTING;
    last_error_.clear();
    std::cout << "Client: Connecting to " << host << ":" << port << "..." << std::endl;
    connection_ = std::make_unique<SocketConnection>();
    if (!connection_->connect(host, port, ec))
    {
        status_ = ClientStatusInternalStatus::STATUS_ERROR;
        last_error_ = "Connect fail: " + (ec ? ec.message() : connection_->getLastErrorMsg());
        connection_.reset();
        std::cerr << "Client: " << last_error_ << std::endl;
        return false;
    }
    std::cout << "Client: TCP connected. Handshaking..." << std::endl;
    if (!performHandshake(ec))
    {
        status_ = ClientStatusInternalStatus::STATUS_ERROR;  // last_error_ set by performHandshake
        if (connection_)
        {
            connection_->close();
            connection_.reset();
        }
        std::cerr << "Client: Handshake fail: " << last_error_ << std::endl;
        return false;
    }
    status_ = ClientStatusInternalStatus::CONNECTED;
    std::cout << "Client: Connected & handshaked with " << host << ":" << port << "." << std::endl;
    return true;
}

void TcpClient::disconnect()
{
    stopKeepAlive();  // Ensure keep-alive thread is stopped first

    std::lock_guard<std::mutex> lock(client_mutex_);
    if (status_ == ClientStatusInternalStatus::DISCONNECTED && !connection_)
        return;
    std::cout << "Client: Disconnecting..." << std::endl;
    if (connection_)
    {
        connection_->close();
        connection_.reset();
    }
    status_ = ClientStatusInternalStatus::DISCONNECTED;
    std::cout << "Client: Disconnected." << std::endl;
}

bool TcpClient::isConnected() const
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    return status_ == ClientStatusInternalStatus::CONNECTED && connection_ && connection_->isOpen();
}

ClientStatusInternalStatus TcpClient::getStatus() const
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    if ((status_ == ClientStatusInternalStatus::CONNECTED ||
         status_ == ClientStatusInternalStatus::PINGING) &&
        (!connection_ || !connection_->isOpen()))
    {
        return ClientStatusInternalStatus::STATUS_ERROR;
    }
    return status_;
}

std::string TcpClient::getLastError() const
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    return last_error_;
}

bool TcpClient::sendMessage(const ISerializable& message, std::error_code& ec)
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (status_ != ClientStatusInternalStatus::CONNECTED || !connection_ || !connection_->isOpen())
    {
        ec = std::make_error_code(std::errc::not_connected);
        last_error_ = "Not connected for sendMessage.";
        return false;
    }
    bool success = TransferInfra::sendMessage(connection_.get(), message, ec);  // Use helper
    if (!success)
    {
        last_error_ = "Client sendMsg type " + std::to_string(message.getMessageType()) +
                      " fail: " + (ec ? ec.message() : connection_->getLastErrorMsg());
        if (ec == std::errc::connection_reset || ec == std::errc::connection_aborted ||
            ec == std::errc::broken_pipe || ec == std::errc::not_connected)
        {
            status_ = ClientStatusInternalStatus::STATUS_ERROR;
            std::cerr << "Client: Conn err during sendMsg. Status set to STATUS_ERROR."
                      << std::endl;
        }
    }
    return success;
}

std::unique_ptr<ISerializable> TcpClient::receiveMessage(int timeout_ms, std::error_code& ec)
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (status_ != ClientStatusInternalStatus::CONNECTED || !connection_ || !connection_->isOpen())
    {
        ec = std::make_error_code(std::errc::not_connected);
        last_error_ = "Not connected for receiveMessage.";
        return nullptr;
    }
    // TODO: Implement actual timeout for readMessage helper if SocketConnection::recv doesn't
    // support it. For now, timeout_ms is conceptual.
    auto msg_ptr = TransferInfra::readMessage(connection_.get(), ec);  // Use helper
    if (!msg_ptr)
    {
        last_error_ = "Client recvMsg fail: " +
                      (ec ? ec.message() :
                            (connection_ ? connection_->getLastErrorMsg() : "Unknown read err"));
        if (ec == std::errc::connection_reset || ec == std::errc::connection_aborted ||
            ec == std::errc::broken_pipe || ec == std::errc::not_connected)
        {
            status_ = ClientStatusInternalStatus::STATUS_ERROR;
            std::cerr << "Client: Conn err during recvMsg. Status set to STATUS_ERROR."
                      << std::endl;
        }
    }
    return msg_ptr;
}

bool TcpClient::pingServer(int timeout_ms, std::error_code& ec)
{
    // This method can be called by the keep-alive thread or directly by user code.
    // It needs its own locking as it might be called from a different thread than other ops.
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (status_ != ClientStatusInternalStatus::CONNECTED)
    {
        ec = std::make_error_code(std::errc::not_connected);
        if (status_ !=
            ClientStatusInternalStatus::PINGING)  // Avoid overwriting "Pinging" status error
            last_error_ = "Ping: Not in CONNECTED state. Current: " +
                          std::to_string(static_cast<int>(status_));
        return false;
    }
    if (!connection_ || !connection_->isOpen())
    {
        ec = std::make_error_code(std::errc::not_connected);
        last_error_ = "Ping: Conn obj null or socket not open.";
        status_ = ClientStatusInternalStatus::STATUS_ERROR;  // Correct status if it was wrong
        return false;
    }

    ClientStatusInternalStatus old_internal_status = status_;  // Store to restore if not error
    status_ = ClientStatusInternalStatus::PINGING;
    std::string ping_op_error_clear;  // Clear error for this specific operation
    std::swap(last_error_, ping_op_error_clear);

    HeartbeatMessage ping;
    ping.timestamp_or_seq = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    std::cout << "TcpClient (Thread:" << std::this_thread::get_id()
              << "): Sending PING (ts:" << ping.timestamp_or_seq << ")..." << std::endl;
    if (!TransferInfra::sendMessage(connection_.get(), ping, ec))
    {
        last_error_ = "Ping send fail:" + (ec ? ec.message() : connection_->getLastErrorMsg());
        status_ = ClientStatusInternalStatus::STATUS_ERROR;
        std::cerr << "TcpClient: " << last_error_ << std::endl;
        return false;
    }

    std::cout << "TcpClient (Thread:" << std::this_thread::get_id()
              << "): Waiting PONG (timeout conceptual:" << timeout_ms << "ms)..." << std::endl;
    // TODO: Implement actual timeout for readMessage via select/poll here.
    auto resp = TransferInfra::readMessage(connection_.get(), ec);

    if (!resp)
    {
        if (!ec)
            ec = std::make_error_code(std::errc::timed_out);
        last_error_ = "Ping resp err/timeout:" + ec.message();
        status_ = ClientStatusInternalStatus::STATUS_ERROR;
        std::cerr << "TcpClient: " << last_error_ << std::endl;
        return false;
    }

    if (resp->getMessageType() == HeartbeatResponseMessage::TYPE_ID)
    {
        auto* pong = dynamic_cast<HeartbeatResponseMessage*>(resp.get());
        if (pong)
        {
            std::cout << "TcpClient (Thread:" << std::this_thread::get_id()
                      << "): PONG recv (orig_ts:" << pong->original_timestamp_or_seq << ")."
                      << std::endl;
            if (pong->original_timestamp_or_seq != ping.timestamp_or_seq)
                std::cout << "TcpClient Warn: Pong ts mismatch." << std::endl;
        }
        else
        {
            ec = std::make_error_code(std::errc::protocol_error);
            last_error_ = "Pong cast fail.";
            status_ = ClientStatusInternalStatus::STATUS_ERROR;
            std::cerr << "TcpClient: " << last_error_ << std::endl;
            return false;
        }
        status_ = ClientStatusInternalStatus::CONNECTED;  // Ping successful, back to connected
        return true;
    }
    else
    {
        ec = std::make_error_code(std::errc::protocol_error);
        last_error_ = "Unexpected msg type instead of PONG:" +
                      std::to_string(static_cast<int>(resp->getMessageType()));
        status_ = ClientStatusInternalStatus::STATUS_ERROR;
        std::cerr << "TcpClient: " << last_error_ << std::endl;
        return false;
    }
}

// --- Keep-Alive Management ---
void TcpClient::startKeepAlive(int interval_sec, int ping_timeout_ms)
{
    std::lock_guard<std::mutex> lock(client_mutex_);  // Protect keep-alive thread and flags
    std::cout << "startKeepAlive started\n";
    if (run_keep_alive_.load())
    {
        std::cout << "TcpClient: Keep-alive thread already running." << std::endl;
        return;
    }
    if (status_ != ClientStatusInternalStatus::CONNECTED)
    {
        last_error_ = "Cannot start keep-alive: Client not connected.";
        std::cerr << "TcpClient: " << last_error_ << std::endl;
        return;
    }

    keep_alive_interval_ = std::chrono::seconds(interval_sec > 0 ? interval_sec : 5);
    keep_alive_ping_timeout_ms_ = ping_timeout_ms > 0 ? ping_timeout_ms : 2000;

    run_keep_alive_.store(true);
    keep_alive_thread_ = std::thread(&TcpClient::keepAliveLoop, this);
    std::cout << "TcpClient: Keep-alive thread started. Interval: " << keep_alive_interval_.count()
              << "s, Ping Timeout: " << keep_alive_ping_timeout_ms_ << "ms." << std::endl;
}

void TcpClient::stopKeepAlive()
{
    if (!run_keep_alive_.exchange(false))
    {  // Set to false and check previous value
        // If already false, ensure thread is joined if it was ever started
        if (keep_alive_thread_.joinable())
        {
            try
            {
                keep_alive_thread_.join();
            }
            catch (const std::system_error& e)
            {
                std::cerr << "TcpClient: Error re-joining keep-alive thread: " << e.what()
                          << std::endl;
            }
        }
        return;
    }

    std::cout << "TcpClient: Stopping keep-alive thread..." << std::endl;
    keep_alive_cv_.notify_one();  // Notify the keep-alive thread to wake up if it's waiting on CV

    if (keep_alive_thread_.joinable())
    {
        try
        {
            keep_alive_thread_.join();
            std::cout << "TcpClient: Keep-alive thread joined." << std::endl;
        }
        catch (const std::system_error& e)
        {
            std::cerr << "TcpClient: System error while joining keep-alive thread: " << e.what()
                      << std::endl;
            // The thread might have already exited if connection was closed by another operation.
        }
    }
    else
    {
        std::cout
        << "TcpClient: Keep-alive thread was not joinable (already finished or not started)."
        << std::endl;
    }
}

void TcpClient::keepAliveLoop()
{
    auto keep_alive_tid = std::this_thread::get_id();
    std::cout << "TcpClient Keep-Alive Thread (" << keep_alive_tid << "): Loop started."
              << std::endl;

    while (run_keep_alive_.load())
    {
        // Use condition variable for timed wait, allowing earlier exit if stopKeepAlive is called
        std::unique_lock<std::mutex> lk(keep_alive_mutex_);
        if (keep_alive_cv_.wait_for(lk, keep_alive_interval_, [this] {
                return !run_keep_alive_.load();
            }))
        {
            // wait_for returned true means stop_flag became true (run_keep_alive_ is false)
            std::cout << "TcpClient Keep-Alive Thread (" << keep_alive_tid
                      << "): Stop signaled via CV." << std::endl;
            break;
        }
        lk.unlock();  // Unlock before pinging

        if (!run_keep_alive_.load())
            break;  // Double check after wait_for timeout

        // Lock client_mutex for isConnected and pingServer (pingServer also locks it)
        // but check basic connection status before potentially long ping
        ClientStatusInternalStatus current_status_val;
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            current_status_val = status_;
        }

        if (current_status_val == ClientStatusInternalStatus::CONNECTED)
        {
            std::cout << "TcpClient Keep-Alive Thread (" << keep_alive_tid
                      << "): Interval elapsed, attempting ping..." << std::endl;
            std::error_code ping_ec;
            // pingServer method already handles locking internally
            if (!this->pingServer(keep_alive_ping_timeout_ms_, ping_ec))
            {
                std::cerr << "TcpClient Keep-Alive Thread (" << keep_alive_tid
                          << "): Ping FAILED. Error: "
                          << (ping_ec ? ping_ec.message() : "Timeout or other error")
                          << " - Client Last Error: " << this->getLastError() << std::endl;
                // If ping fails, TcpClient's status is likely STATUS_ERROR.
                // The keep-alive thread should then stop itself.
                std::lock_guard<std::mutex> lock(client_mutex_);  // Re-lock to check status safely
                if (status_ == ClientStatusInternalStatus::STATUS_ERROR)
                {
                    std::cout
                    << "TcpClient Keep-Alive Thread (" << keep_alive_tid
                    << "): Client status is STATUS_ERROR after ping failure, stopping keep-alive."
                    << std::endl;
                    run_keep_alive_.store(false);  // Signal this thread to stop
                }
            }
            else
            {
                std::cout << "TcpClient Keep-Alive Thread (" << keep_alive_tid
                          << "): Ping successful." << std::endl;
            }
        }
        else
        {
            std::cout << "TcpClient Keep-Alive Thread (" << keep_alive_tid
                      << "): Client not in CONNECTED state ("
                      << static_cast<int>(current_status_val) << "), skipping ping." << std::endl;
            if (current_status_val == ClientStatusInternalStatus::DISCONNECTED ||
                current_status_val == ClientStatusInternalStatus::STATUS_ERROR)
            {
                std::cout << "TcpClient Keep-Alive Thread (" << keep_alive_tid
                          << "): Client disconnected or in error state, stopping keep-alive."
                          << std::endl;
                run_keep_alive_.store(false);  // Stop this thread
            }
        }
    }
    std::cout << "TcpClient Keep-Alive Thread (" << keep_alive_tid << ") exiting." << std::endl;
}

std::string TcpClient::startCapture(std::error_code& ec)
{
    std::cout << "Client: Starting Capture..." << std::endl;
    std::lock_guard<std::mutex> lock(client_mutex_);  // Protect status_ and connection_
    CaptureRequest              req;
    req.data_field = "start_capture";
    std::cout << "Client: Sending startCapture (Client " << req.data_field << ")" << std::endl;
    if (!TransferInfra::sendMessage(connection_.get(), req, ec))
    {
        last_error_ = "Start Capture send fail: " +
                      (ec ? ec.message() : connection_->getLastErrorMsg());
        return "";
    }
    std::cout << "Client: Waiting for Capture response..." << std::endl;
    auto response_base = TransferInfra::readMessage(connection_.get(), ec);
    if (!response_base)
    {
        last_error_ = "Capture recv fail: " +
                      (ec ? ec.message() :
                            (connection_ ? connection_->getLastErrorMsg() : "Unknown read err"));
        if (ec == std::errc::connection_aborted)
            std::cout << "Client: Server closed during Capture recv." << std::endl;
        return "";
    }
    if (response_base->getMessageType() == CaptureResponse::TYPE_ID)
    {
        auto* hs_recv = dynamic_cast<CaptureResponse*>(response_base.get());
        if (hs_recv)
        {
            std::cout << "Client: Server Capture (Server " << hs_recv->data_field << ")"
                      << std::endl;
            return hs_recv->data_field;
        }
    }
    ec = std::make_error_code(std::errc::protocol_error);
    last_error_ = "Unexpected msg type in Capture (Expected: " +
                  std::to_string(HandShakeMessage::TYPE_ID) +
                  ", Got: " + std::to_string(response_base->getMessageType()) + ").";
    return "";
}

}  // namespace TransferInfra