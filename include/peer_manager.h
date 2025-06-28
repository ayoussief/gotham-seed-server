#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <cstdint>

/**
 * @brief Manages active peer list for the seed server
 * 
 * Handles peer registration, discovery, and cleanup while maintaining privacy.
 */
class PeerManager {
public:
    struct PeerInfo {
        std::string onion_address;
        uint16_t port;
        uint32_t capabilities;
        std::chrono::steady_clock::time_point last_seen;
        std::chrono::steady_clock::time_point registered_at;
        uint32_t request_count;  // For rate limiting
        
        PeerInfo() : port(0), capabilities(0), request_count(0) {
            auto now = std::chrono::steady_clock::now();
            last_seen = now;
            registered_at = now;
        }
    };
    
    struct Stats {
        size_t total_peers;
        size_t active_peers;
        size_t requests_served;
        size_t registrations_processed;
        std::chrono::steady_clock::time_point server_start_time;
        
        Stats() : total_peers(0), active_peers(0), requests_served(0), 
                 registrations_processed(0) {
            server_start_time = std::chrono::steady_clock::now();
        }
    };

    /**
     * @brief Construct a new Peer Manager
     * 
     * @param max_peers Maximum number of peers to track
     * @param rate_limit_per_minute Maximum requests per peer per minute
     */
    PeerManager(size_t max_peers = 500, uint32_t rate_limit_per_minute = 60);
    
    /**
     * @brief Register a peer
     * 
     * @param onion_address Peer's .onion address
     * @param port Peer's listening port
     * @param capabilities Peer's capability flags
     * @return true if registered successfully, false if rejected
     */
    bool registerPeer(const std::string& onion_address, uint16_t port, uint32_t capabilities);
    
    /**
     * @brief Unregister a peer
     * 
     * @param onion_address Peer's .onion address
     * @return true if unregistered, false if not found
     */
    bool unregisterPeer(const std::string& onion_address);
    
    /**
     * @brief Get list of active peers for discovery
     * 
     * @param requesting_peer Address of peer making the request (for rate limiting)
     * @param max_peers Maximum number of peers to return
     * @param required_capabilities Required capability flags (0 = any)
     * @return std::vector<PeerInfo> List of peers matching criteria
     */
    std::vector<PeerInfo> getPeersForDiscovery(const std::string& requesting_peer, 
                                              size_t max_peers = 20,
                                              uint32_t required_capabilities = 0);
    
    /**
     * @brief Update peer's last seen timestamp
     * 
     * @param onion_address Peer's .onion address
     */
    void updatePeerActivity(const std::string& onion_address);
    
    /**
     * @brief Remove inactive peers
     * 
     * @param max_age_seconds Maximum age in seconds before peer is considered inactive
     * @return size_t Number of peers removed
     */
    size_t cleanupInactivePeers(uint32_t max_age_seconds = 300);
    
    /**
     * @brief Check if peer is rate limited
     * 
     * @param onion_address Peer's .onion address
     * @return true if rate limited, false if allowed
     */
    bool isRateLimited(const std::string& onion_address);
    
    /**
     * @brief Get current statistics
     * 
     * @return Stats Current peer manager statistics
     */
    Stats getStats() const;
    
    /**
     * @brief Validate .onion address format
     * 
     * @param address Address to validate
     * @return true if valid .onion address, false otherwise
     */
    static bool isValidOnionAddress(const std::string& address);

private:
    mutable std::mutex peers_mutex_;
    std::unordered_map<std::string, PeerInfo> peers_;
    
    const size_t max_peers_;
    const uint32_t rate_limit_per_minute_;
    
    Stats stats_;
    
    /**
     * @brief Clean up rate limiting counters
     */
    void cleanupRateLimiting();
    
    /**
     * @brief Get random subset of peers
     * 
     * @param peers Source peer list
     * @param max_count Maximum number to return
     * @return std::vector<PeerInfo> Random subset
     */
    std::vector<PeerInfo> getRandomSubset(const std::vector<PeerInfo>& peers, size_t max_count);
};