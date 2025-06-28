#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <cstdint>
#include <mutex>
#include "gcty_protocol.h"

class PeerManager;

/**
 * @brief Handles GCTY protocol messages for the seed server
 * 
 * Processes incoming GCTY protocol messages and generates appropriate responses.
 */
class GCTYHandler {
public:
    using ResponseCallback = std::function<void(const std::vector<uint8_t>&)>;
    
    /**
     * @brief Construct a new GCTY Handler
     * 
     * @param peer_manager Shared peer manager instance
     */
    explicit GCTYHandler(std::shared_ptr<PeerManager> peer_manager);
    
    /**
     * @brief Process incoming GCTY message
     * 
     * @param data Raw message data
     * @param peer_address Address of sending peer (for rate limiting)
     * @param response_callback Callback to send response
     * @return true if message processed successfully, false otherwise
     */
    bool processMessage(const std::vector<uint8_t>& data, 
                       const std::string& peer_address,
                       ResponseCallback response_callback);
    
    /**
     * @brief Get handler statistics
     * 
     * @return std::string Statistics in human-readable format
     */
    std::string getStats() const;

private:
    std::shared_ptr<PeerManager> peer_manager_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    uint64_t messages_processed_;
    uint64_t invalid_messages_;
    uint64_t rate_limited_requests_;
    uint64_t peer_registrations_;
    uint64_t peer_discoveries_;
    uint64_t ping_requests_;
    
    /**
     * @brief Handle peer registration request
     * 
     * @param payload Message payload
     * @param peer_address Requesting peer address
     * @param response_callback Response callback
     * @return true if handled successfully
     */
    bool handlePeerRegister(const std::vector<uint8_t>& payload,
                           const std::string& peer_address,
                           ResponseCallback response_callback);
    
    /**
     * @brief Handle peer discovery request
     * 
     * @param payload Message payload
     * @param peer_address Requesting peer address
     * @param response_callback Response callback
     * @return true if handled successfully
     */
    bool handlePeerDiscovery(const std::vector<uint8_t>& payload,
                            const std::string& peer_address,
                            ResponseCallback response_callback);
    
    /**
     * @brief Handle peer unregister request
     * 
     * @param payload Message payload
     * @param peer_address Requesting peer address
     * @param response_callback Response callback
     * @return true if handled successfully
     */
    bool handlePeerUnregister(const std::vector<uint8_t>& payload,
                             const std::string& peer_address,
                             ResponseCallback response_callback);
    
    /**
     * @brief Handle ping request
     * 
     * @param payload Message payload
     * @param peer_address Requesting peer address
     * @param response_callback Response callback
     * @return true if handled successfully
     */
    bool handlePing(const std::vector<uint8_t>& payload,
                   const std::string& peer_address,
                   ResponseCallback response_callback);
    
    /**
     * @brief Send error response
     * 
     * @param error_code Error code
     * @param error_message Error message
     * @param response_callback Response callback
     */
    void sendErrorResponse(uint8_t error_code,
                          const std::string& error_message,
                          ResponseCallback response_callback);
    
    /**
     * @brief Create success response
     * 
     * @param message_type Response message type
     * @param payload Response payload
     * @return std::vector<uint8_t> Complete response message
     */
    std::vector<uint8_t> createSuccessResponse(gcty_protocol::MessageType message_type,
                                              const std::vector<uint8_t>& payload);
};