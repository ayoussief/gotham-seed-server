#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

/**
 * @brief Gotham City Network Protocol Definitions (Seed Server Version)
 * 
 * Self-contained implementation of the GCTY protocol for the seed server.
 * This is independent of the main Gotham City project.
 */
namespace gcty_protocol {

// Protocol constants
static const uint32_t MAGIC_BYTES = 0x47435459;  // "GCTY" in hex
static const uint16_t PROTOCOL_VERSION = 1;
static const uint32_t MAX_MESSAGE_SIZE = 1024 * 1024;  // 1MB limit

// Message types for seed server
enum class MessageType : uint8_t {
    HANDSHAKE_REQUEST = 0x01,
    HANDSHAKE_RESPONSE = 0x02,
    PEER_REGISTER = 0x12,
    PEER_DISCOVERY = 0x13,
    PEER_UNREGISTER = 0x14,
    PING = 0xF0,
    PONG = 0xF1,
    ERROR_RESPONSE = 0xFF
};

// Node capabilities (what features a peer supports)
enum class NodeCapabilities : uint32_t {
    BASIC_MESSAGING = 0x00000001,
    DHT_STORAGE = 0x00000002,
    FILE_SHARING = 0x00000004,
    VOICE_CHAT = 0x00000008,
    VIDEO_CHAT = 0x00000010,
    GAME_HOSTING = 0x00000020
};

/**
 * @brief Standard GCTY protocol message header
 * 
 * All messages start with this header followed by the payload.
 */
struct MessageHeader {
    uint32_t magic;           // Magic bytes (0x47435459)
    uint16_t version;         // Protocol version
    uint8_t type;            // Message type (MessageType enum)
    uint8_t flags;           // Message flags (reserved)
    uint32_t payload_length; // Length of payload in bytes
    uint32_t checksum;       // CRC32 checksum of payload
    
    MessageHeader() : magic(MAGIC_BYTES), version(PROTOCOL_VERSION), 
                     type(0), flags(0), payload_length(0), checksum(0) {}
} __attribute__((packed));

/**
 * @brief Peer registration request
 */
struct PeerRegisterRequest {
    uint16_t port;
    uint32_t capabilities;
    char onion_address[64];  // Null-terminated .onion address
    
    PeerRegisterRequest() : port(0), capabilities(0) {
        memset(onion_address, 0, sizeof(onion_address));
    }
} __attribute__((packed));

/**
 * @brief Peer discovery request
 */
struct PeerDiscoveryRequest {
    uint16_t max_peers;      // Maximum peers to return
    uint32_t required_capabilities;  // Required capability flags
    uint32_t reserved;       // Reserved for future use
    
    PeerDiscoveryRequest() : max_peers(20), required_capabilities(0), reserved(0) {}
} __attribute__((packed));

/**
 * @brief Peer discovery response header
 */
struct PeerDiscoveryResponse {
    uint16_t peer_count;     // Number of peers in response
    uint16_t reserved;       // Reserved for future use
    
    PeerDiscoveryResponse() : peer_count(0), reserved(0) {}
} __attribute__((packed));

/**
 * @brief Individual peer entry in discovery response
 */
struct PeerEntry {
    uint16_t port;
    uint32_t capabilities;
    char onion_address[64];  // Null-terminated .onion address
    
    PeerEntry() : port(0), capabilities(0) {
        memset(onion_address, 0, sizeof(onion_address));
    }
} __attribute__((packed));

/**
 * @brief Error response
 */
struct ErrorResponse {
    uint8_t error_code;
    uint8_t reserved[3];
    char error_message[128];  // Null-terminated error message
    
    ErrorResponse() : error_code(0) {
        memset(reserved, 0, sizeof(reserved));
        memset(error_message, 0, sizeof(error_message));
    }
} __attribute__((packed));

/**
 * @brief Protocol utility functions
 */
class ProtocolUtils {
public:
    /**
     * @brief Create a complete GCTY message
     * 
     * @param type Message type
     * @param payload Message payload
     * @return std::vector<uint8_t> Complete message with header
     */
    static std::vector<uint8_t> createMessage(MessageType type, const std::vector<uint8_t>& payload);
    
    /**
     * @brief Parse a GCTY message
     * 
     * @param data Raw message data
     * @param header Output header
     * @param payload Output payload
     * @return true if parsing successful, false otherwise
     */
    static bool parseMessage(const std::vector<uint8_t>& data, MessageHeader& header, std::vector<uint8_t>& payload);
    
    /**
     * @brief Convert header from host to network byte order
     * 
     * @param header Header to convert
     */
    static void hostToNetwork(MessageHeader& header);
    
    /**
     * @brief Convert header from network to host byte order
     * 
     * @param header Header to convert
     */
    static void networkToHost(MessageHeader& header);
    
    /**
     * @brief Calculate CRC32 checksum
     * 
     * @param data Data to checksum
     * @return uint32_t CRC32 checksum
     */
    static uint32_t calculateCRC32(const std::vector<uint8_t>& data);
    
    /**
     * @brief Validate message integrity
     * 
     * @param header Message header
     * @param payload Message payload
     * @return true if message is valid, false otherwise
     */
    static bool validateMessage(const MessageHeader& header, const std::vector<uint8_t>& payload);
};

} // namespace gcty_protocol