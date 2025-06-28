#include "gcty_handler.h"
#include "peer_manager.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <sstream>
#include <mutex>

// Use the self-contained protocol
using namespace gcty_protocol;

GCTYHandler::GCTYHandler(std::shared_ptr<PeerManager> peer_manager)
    : peer_manager_(peer_manager), messages_processed_(0), invalid_messages_(0),
      rate_limited_requests_(0), peer_registrations_(0), peer_discoveries_(0), ping_requests_(0) {
    
    std::cout << "🔧 GCTY Handler initialized" << std::endl;
}

bool GCTYHandler::processMessage(const std::vector<uint8_t>& data, 
                                const std::string& peer_address,
                                ResponseCallback response_callback) {
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    messages_processed_++;
    
    // Parse message
    MessageHeader header;
    std::vector<uint8_t> payload;
    
    if (!ProtocolUtils::parseMessage(data, header, payload)) {
        invalid_messages_++;
        sendErrorResponse(1, "Invalid GCTY message format", response_callback);
        return false;
    }
    
    // Check rate limiting
    if (peer_manager_->isRateLimited(peer_address)) {
        rate_limited_requests_++;
        sendErrorResponse(2, "Rate limit exceeded", response_callback);
        return false;
    }
    
    // Update peer activity
    peer_manager_->updatePeerActivity(peer_address);
    
    // Dispatch based on message type
    bool handled = false;
    MessageType msg_type = static_cast<MessageType>(header.type);
    
    switch (msg_type) {
        case MessageType::PEER_REGISTER:
            handled = handlePeerRegister(payload, peer_address, response_callback);
            if (handled) peer_registrations_++;
            break;
            
        case MessageType::PEER_DISCOVERY:
            handled = handlePeerDiscovery(payload, peer_address, response_callback);
            if (handled) peer_discoveries_++;
            break;
            
        case MessageType::PEER_UNREGISTER:
            handled = handlePeerUnregister(payload, peer_address, response_callback);
            break;
            
        case MessageType::PING:
            handled = handlePing(payload, peer_address, response_callback);
            if (handled) ping_requests_++;
            break;
            
        default:
            sendErrorResponse(3, "Unsupported message type", response_callback);
            handled = false;
            break;
    }
    
    if (!handled) {
        invalid_messages_++;
    }
    
    return handled;
}

std::string GCTYHandler::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    std::ostringstream oss;
    oss << "GCTY Handler Statistics:\n";
    oss << "  Messages Processed: " << messages_processed_ << "\n";
    oss << "  Invalid Messages: " << invalid_messages_ << "\n";
    oss << "  Rate Limited: " << rate_limited_requests_ << "\n";
    oss << "  Peer Registrations: " << peer_registrations_ << "\n";
    oss << "  Peer Discoveries: " << peer_discoveries_ << "\n";
    oss << "  Ping Requests: " << ping_requests_;
    
    return oss.str();
}

bool GCTYHandler::handlePeerRegister(const std::vector<uint8_t>& payload,
                                    const std::string& peer_address,
                                    ResponseCallback response_callback) {
    
    if (payload.size() != sizeof(PeerRegisterRequest)) {
        sendErrorResponse(4, "Invalid peer register payload size", response_callback);
        return false;
    }
    
    PeerRegisterRequest request;
    memcpy(&request, payload.data(), sizeof(request));
    
    // Convert from network byte order
    request.port = ntohs(request.port);
    request.capabilities = ntohl(request.capabilities);
    
    // Ensure null termination
    request.onion_address[sizeof(request.onion_address) - 1] = '\0';
    std::string onion_address(request.onion_address);
    
    // Validate the address
    if (!PeerManager::isValidOnionAddress(onion_address)) {
        sendErrorResponse(5, "Invalid onion address format", response_callback);
        return false;
    }
    
    // Register the peer
    if (peer_manager_->registerPeer(onion_address, request.port, request.capabilities)) {
        // Send success response
        auto response = createSuccessResponse(MessageType::HANDSHAKE_RESPONSE, {});
        response_callback(response);
        return true;
    } else {
        sendErrorResponse(6, "Failed to register peer (capacity reached)", response_callback);
        return false;
    }
}

bool GCTYHandler::handlePeerDiscovery(const std::vector<uint8_t>& payload,
                                     const std::string& peer_address,
                                     ResponseCallback response_callback) {
    
    PeerDiscoveryRequest request;
    if (payload.size() >= sizeof(request)) {
        memcpy(&request, payload.data(), sizeof(request));
        // Convert from network byte order
        request.max_peers = ntohs(request.max_peers);
        request.required_capabilities = ntohl(request.required_capabilities);
    }
    // If payload is smaller, use default values
    
    // Limit max_peers to reasonable value
    if (request.max_peers > 50) {
        request.max_peers = 50;
    }
    
    // Get peers from manager
    auto peers = peer_manager_->getPeersForDiscovery(peer_address, request.max_peers, request.required_capabilities);
    
    // Create response
    PeerDiscoveryResponse response_header;
    response_header.peer_count = htons(static_cast<uint16_t>(peers.size()));
    
    std::vector<uint8_t> response_payload;
    
    // Add response header
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&response_header);
    response_payload.insert(response_payload.end(), header_bytes, header_bytes + sizeof(response_header));
    
    // Add peer entries
    for (const auto& peer : peers) {
        PeerEntry entry;
        entry.port = htons(peer.port);
        entry.capabilities = htonl(peer.capabilities);
        strncpy(entry.onion_address, peer.onion_address.c_str(), sizeof(entry.onion_address) - 1);
        
        const uint8_t* entry_bytes = reinterpret_cast<const uint8_t*>(&entry);
        response_payload.insert(response_payload.end(), entry_bytes, entry_bytes + sizeof(entry));
    }
    
    // Send response
    auto response = createSuccessResponse(MessageType::HANDSHAKE_RESPONSE, response_payload);
    response_callback(response);
    
    return true;
}

bool GCTYHandler::handlePeerUnregister(const std::vector<uint8_t>& payload,
                                      const std::string& peer_address,
                                      ResponseCallback response_callback) {
    
    // For unregister, we can just use the peer_address from the connection
    // The payload could contain additional verification, but for simplicity we'll use the connection address
    
    if (peer_manager_->unregisterPeer(peer_address)) {
        auto response = createSuccessResponse(MessageType::HANDSHAKE_RESPONSE, {});
        response_callback(response);
        return true;
    } else {
        sendErrorResponse(7, "Peer not found for unregistration", response_callback);
        return false;
    }
}

bool GCTYHandler::handlePing(const std::vector<uint8_t>& payload,
                            const std::string& peer_address,
                            ResponseCallback response_callback) {
    
    // Simple ping/pong - just echo back a pong
    auto response = createSuccessResponse(MessageType::PONG, payload);
    response_callback(response);
    
    return true;
}

void GCTYHandler::sendErrorResponse(uint8_t error_code,
                                   const std::string& error_message,
                                   ResponseCallback response_callback) {
    
    ErrorResponse error;
    error.error_code = error_code;
    strncpy(error.error_message, error_message.c_str(), sizeof(error.error_message) - 1);
    
    std::vector<uint8_t> payload(reinterpret_cast<const uint8_t*>(&error),
                                reinterpret_cast<const uint8_t*>(&error) + sizeof(error));
    
    // Use the error response message type
    auto response = createSuccessResponse(MessageType::ERROR_RESPONSE, payload);
    response_callback(response);
}

std::vector<uint8_t> GCTYHandler::createSuccessResponse(gcty_protocol::MessageType message_type,
                                                       const std::vector<uint8_t>& payload) {
    
    return ProtocolUtils::createMessage(message_type, payload);
}