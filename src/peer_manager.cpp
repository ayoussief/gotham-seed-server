#include "peer_manager.h"
#include <algorithm>
#include <random>
#include <regex>
#include <iostream>

PeerManager::PeerManager(size_t max_peers, uint32_t rate_limit_per_minute)
    : max_peers_(max_peers), rate_limit_per_minute_(rate_limit_per_minute) {
    
    std::cout << "📋 PeerManager initialized (max_peers: " << max_peers_ 
              << ", rate_limit: " << rate_limit_per_minute_ << "/min)" << std::endl;
}

bool PeerManager::registerPeer(const std::string& onion_address, uint16_t port, uint32_t capabilities) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    // Validate onion address
    if (!isValidOnionAddress(onion_address)) {
        return false;
    }
    
    // Check if we're at capacity
    if (peers_.size() >= max_peers_ && peers_.find(onion_address) == peers_.end()) {
        return false; // At capacity and this is a new peer
    }
    
    // Create or update peer info
    PeerInfo& peer = peers_[onion_address];
    peer.onion_address = onion_address;
    peer.port = port;
    peer.capabilities = capabilities;
    peer.last_seen = std::chrono::steady_clock::now();
    
    // If this is a new registration, set registered_at
    if (peer.registered_at == std::chrono::steady_clock::time_point{}) {
        peer.registered_at = peer.last_seen;
        stats_.registrations_processed++;
    }
    
    stats_.total_peers = peers_.size();
    return true;
}

bool PeerManager::unregisterPeer(const std::string& onion_address) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    auto it = peers_.find(onion_address);
    if (it != peers_.end()) {
        peers_.erase(it);
        stats_.total_peers = peers_.size();
        return true;
    }
    
    return false;
}

std::vector<PeerManager::PeerInfo> PeerManager::getPeersForDiscovery(
    const std::string& requesting_peer, size_t max_peers, uint32_t required_capabilities) {
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    // Check rate limiting
    if (isRateLimited(requesting_peer)) {
        return {}; // Return empty list if rate limited
    }
    
    // Update request count for rate limiting
    auto it = peers_.find(requesting_peer);
    if (it != peers_.end()) {
        it->second.request_count++;
    }
    
    // Collect eligible peers
    std::vector<PeerInfo> eligible_peers;
    auto now = std::chrono::steady_clock::now();
    
    for (const auto& [address, peer] : peers_) {
        // Don't include the requesting peer in the list
        if (address == requesting_peer) {
            continue;
        }
        
        // Check if peer is still active (seen within last 5 minutes)
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - peer.last_seen).count();
        if (age > 300) {
            continue;
        }
        
        // Check capability requirements
        if (required_capabilities != 0 && (peer.capabilities & required_capabilities) != required_capabilities) {
            continue;
        }
        
        eligible_peers.push_back(peer);
    }
    
    stats_.active_peers = eligible_peers.size();
    stats_.requests_served++;
    
    // Return random subset
    return getRandomSubset(eligible_peers, max_peers);
}

void PeerManager::updatePeerActivity(const std::string& onion_address) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    auto it = peers_.find(onion_address);
    if (it != peers_.end()) {
        it->second.last_seen = std::chrono::steady_clock::now();
    }
}

size_t PeerManager::cleanupInactivePeers(uint32_t max_age_seconds) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    size_t removed_count = 0;
    
    auto it = peers_.begin();
    while (it != peers_.end()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_seen).count();
        
        if (age > max_age_seconds) {
            it = peers_.erase(it);
            removed_count++;
        } else {
            ++it;
        }
    }
    
    // Also cleanup rate limiting counters
    cleanupRateLimiting();
    
    stats_.total_peers = peers_.size();
    return removed_count;
}

bool PeerManager::isRateLimited(const std::string& onion_address) {
    // This is called with peers_mutex_ already locked
    
    auto it = peers_.find(onion_address);
    if (it == peers_.end()) {
        return false; // Unknown peer, not rate limited
    }
    
    // Simple rate limiting: reset counter every minute
    auto now = std::chrono::steady_clock::now();
    auto time_since_last_reset = std::chrono::duration_cast<std::chrono::seconds>(
        now - it->second.last_seen).count();
    
    if (time_since_last_reset >= 60) {
        // Reset counter
        it->second.request_count = 0;
        return false;
    }
    
    return it->second.request_count >= rate_limit_per_minute_;
}

PeerManager::Stats PeerManager::getStats() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    // Count active peers (seen within last 5 minutes)
    auto now = std::chrono::steady_clock::now();
    size_t active_count = 0;
    
    for (const auto& [address, peer] : peers_) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - peer.last_seen).count();
        if (age <= 300) {
            active_count++;
        }
    }
    
    Stats current_stats = stats_;
    current_stats.total_peers = peers_.size();
    current_stats.active_peers = active_count;
    
    return current_stats;
}

bool PeerManager::isValidOnionAddress(const std::string& address) {
    // Basic validation for .onion addresses
    // v2: 16 characters + .onion = 22 total
    // v3: 56 characters + .onion = 62 total
    
    if (address.length() != 22 && address.length() != 62) {
        return false;
    }
    
    if (!address.ends_with(".onion")) {
        return false;
    }
    
    // Check that the part before .onion contains only valid base32 characters
    std::string onion_part = address.substr(0, address.length() - 6);
    std::regex base32_regex("^[a-z2-7]+$");
    
    return std::regex_match(onion_part, base32_regex);
}

void PeerManager::cleanupRateLimiting() {
    // This is called with peers_mutex_ already locked
    
    auto now = std::chrono::steady_clock::now();
    
    for (auto& [address, peer] : peers_) {
        auto time_since_last_seen = std::chrono::duration_cast<std::chrono::seconds>(
            now - peer.last_seen).count();
        
        // Reset rate limiting counter if more than a minute has passed
        if (time_since_last_seen >= 60) {
            peer.request_count = 0;
        }
    }
}

std::vector<PeerManager::PeerInfo> PeerManager::getRandomSubset(
    const std::vector<PeerInfo>& peers, size_t max_count) {
    
    if (peers.size() <= max_count) {
        return peers;
    }
    
    // Create a copy and shuffle it
    std::vector<PeerInfo> shuffled = peers;
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::shuffle(shuffled.begin(), shuffled.end(), gen);
    
    // Return first max_count elements
    shuffled.resize(max_count);
    return shuffled;
}