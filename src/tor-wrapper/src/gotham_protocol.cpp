#include "gotham_protocol.h"
#include <chrono>
#include <random>
#include <cstring>
#include <arpa/inet.h>

namespace gotham_protocol {

bool ProtocolUtils::validateHeader(const MessageHeader& header) {
    // Check magic bytes
    if (header.magic != MAGIC_BYTES) {
        return false;
    }
    
    // Check version (for now, only version 1 is supported)
    if (header.version != PROTOCOL_VERSION) {
        return false;
    }
    
    // Check reserved fields are zero
    if (header.reserved != 0 || header.flags != 0 || header.padding != 0) {
        return false;
    }
    
    // Check payload length is reasonable
    if (header.payload_length > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    return true;
}

std::vector<uint8_t> ProtocolUtils::createMessage(MessageType type, const std::vector<uint8_t>& payload) {
    MessageHeader header;
    header.type = type;
    header.payload_length = static_cast<uint32_t>(payload.size());
    
    // Convert to network byte order
    hostToNetwork(header);
    
    // Create complete message
    std::vector<uint8_t> message;
    message.reserve(sizeof(MessageHeader) + payload.size());
    
    // Add header
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    message.insert(message.end(), header_bytes, header_bytes + sizeof(MessageHeader));
    
    // Add payload
    message.insert(message.end(), payload.begin(), payload.end());
    
    return message;
}

bool ProtocolUtils::parseMessage(const std::vector<uint8_t>& data, MessageHeader& header, std::vector<uint8_t>& payload) {
    // Check minimum size
    if (data.size() < sizeof(MessageHeader)) {
        return false;
    }
    
    // Extract header
    memcpy(&header, data.data(), sizeof(MessageHeader));
    
    // Convert from network byte order
    networkToHost(header);
    
    // Validate header
    if (!validateHeader(header)) {
        return false;
    }
    
    // Check total message size
    if (data.size() != sizeof(MessageHeader) + header.payload_length) {
        return false;
    }
    
    // Extract payload
    payload.clear();
    if (header.payload_length > 0) {
        payload.assign(data.begin() + sizeof(MessageHeader), data.end());
    }
    
    return true;
}

void ProtocolUtils::networkToHost(MessageHeader& header) {
    header.magic = ntohl(header.magic);
    header.version = ntohs(header.version);
    header.reserved = ntohs(header.reserved);
    // type, flags, padding are single bytes - no conversion needed
    header.payload_length = ntohl(header.payload_length);
}

void ProtocolUtils::hostToNetwork(MessageHeader& header) {
    header.magic = htonl(header.magic);
    header.version = htons(header.version);
    header.reserved = htons(header.reserved);
    // type, flags, padding are single bytes - no conversion needed
    header.payload_length = htonl(header.payload_length);
}

uint64_t ProtocolUtils::getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void ProtocolUtils::generateNodeId(char* node_id) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (int i = 0; i < 32; ++i) {
        node_id[i] = static_cast<char>(dis(gen));
    }
}

} // namespace gotham_protocol