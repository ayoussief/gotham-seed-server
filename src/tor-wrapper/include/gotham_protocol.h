#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

/**
 * @brief Gotham City Network Protocol Definitions
 * 
 * This file defines the network protocol used by Gotham City nodes
 * for secure peer-to-peer communication over Tor.
 */

namespace gotham_protocol {

// Protocol constants
static const uint32_t MAGIC_BYTES = 0x47435459;  // "GCTY" in hex
static const uint16_t PROTOCOL_VERSION = 1;
static const uint32_t MAX_MESSAGE_SIZE = 1024 * 1024;  // 1MB limit

// Message types
enum class MessageType : uint8_t {
    HANDSHAKE_REQUEST = 0x01,
    HANDSHAKE_RESPONSE = 0x02,
    PEER_MESSAGE = 0x10,
    PEER_BROADCAST = 0x11,
    PEER_REGISTER = 0x12,
    PEER_DISCOVERY = 0x13,
    PEER_UNREGISTER = 0x14,
    DHT_STORE = 0x20,
    DHT_FIND = 0x21,
    DHT_RESPONSE = 0x22,
    PING = 0xF0,
    PONG = 0xF1
};

/**
 * @brief Standard Gotham protocol message header
 * 
 * All messages start with this header to identify Gotham traffic
 * and provide basic message information.
 */
struct MessageHeader {
    uint32_t magic;          // Always MAGIC_BYTES (0x47435459)
    uint16_t version;        // Protocol version
    uint16_t reserved;       // Reserved for future use (must be 0)
    MessageType type;        // Message type
    uint8_t flags;           // Message flags (reserved, must be 0)
    uint16_t padding;        // Padding to align to 4-byte boundary
    uint32_t payload_length; // Length of payload following this header
    
    MessageHeader() : magic(MAGIC_BYTES), version(PROTOCOL_VERSION), 
                     reserved(0), type(MessageType::PEER_MESSAGE), 
                     flags(0), padding(0), payload_length(0) {}
} __attribute__((packed));

static_assert(sizeof(MessageHeader) == 16, "MessageHeader must be exactly 16 bytes");

/**
 * @brief Handshake request payload
 */
struct HandshakeRequest {
    uint64_t timestamp;      // Current timestamp
    uint32_t capabilities;   // Node capabilities bitfield
    uint16_t listen_port;    // Port this node listens on
    uint16_t reserved;       // Reserved for future use
    char node_id[32];        // Node identifier (public key hash)
    char user_agent[64];     // User agent string (null-terminated)
    
    HandshakeRequest() : timestamp(0), capabilities(0), listen_port(0), 
                        reserved(0) {
        memset(node_id, 0, sizeof(node_id));
        memset(user_agent, 0, sizeof(user_agent));
        strncpy(user_agent, "GothamCity/1.0", sizeof(user_agent) - 1);
    }
} __attribute__((packed));

/**
 * @brief Handshake response payload
 */
struct HandshakeResponse {
    uint64_t timestamp;      // Current timestamp
    uint32_t capabilities;   // Node capabilities bitfield
    uint16_t listen_port;    // Port this node listens on
    uint8_t status;          // Handshake status (0 = success, 1 = rejected)
    uint8_t reserved;        // Reserved for future use
    char node_id[32];        // Node identifier (public key hash)
    char user_agent[64];     // User agent string (null-terminated)
    
    HandshakeResponse() : timestamp(0), capabilities(0), listen_port(0), 
                         status(0), reserved(0) {
        memset(node_id, 0, sizeof(node_id));
        memset(user_agent, 0, sizeof(user_agent));
        strncpy(user_agent, "GothamCity/1.0", sizeof(user_agent) - 1);
    }
} __attribute__((packed));

/**
 * @brief Node capabilities bitfield
 */
enum class NodeCapabilities : uint32_t {
    BASIC_MESSAGING = 0x00000001,    // Basic peer messaging
    DHT_STORAGE = 0x00000002,        // DHT storage and retrieval
    GAME_ENGINE = 0x00000004,        // Game engine support
    AUTH_BRIDGE = 0x00000008,        // Authentication bridge
    SEED_SERVER = 0x00000010,        // Seed server functionality
};

/**
 * @brief Utility functions for protocol handling
 */
class ProtocolUtils {
public:
    /**
     * @brief Validate message header
     * @param header Header to validate
     * @return true if valid, false otherwise
     */
    static bool validateHeader(const MessageHeader& header);
    
    /**
     * @brief Create a message with proper header
     * @param type Message type
     * @param payload Payload data
     * @return Complete message with header
     */
    static std::vector<uint8_t> createMessage(MessageType type, const std::vector<uint8_t>& payload);
    
    /**
     * @brief Parse message from raw data
     * @param data Raw message data
     * @param header Output header
     * @param payload Output payload
     * @return true if parsed successfully, false otherwise
     */
    static bool parseMessage(const std::vector<uint8_t>& data, MessageHeader& header, std::vector<uint8_t>& payload);
    
    /**
     * @brief Convert network byte order to host byte order for header
     * @param header Header to convert (modified in place)
     */
    static void networkToHost(MessageHeader& header);
    
    /**
     * @brief Convert host byte order to network byte order for header
     * @param header Header to convert (modified in place)
     */
    static void hostToNetwork(MessageHeader& header);
    
    /**
     * @brief Get current timestamp in milliseconds
     * @return Current timestamp
     */
    static uint64_t getCurrentTimestamp();
    
    /**
     * @brief Generate random node ID
     * @param node_id Output buffer (must be 32 bytes)
     */
    static void generateNodeId(char* node_id);
};

} // namespace gotham_protocol