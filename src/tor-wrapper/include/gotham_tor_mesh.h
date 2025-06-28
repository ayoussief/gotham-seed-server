#pragma once

#include "tor_service.h"
#include "onion_identity_manager.h"
#include "gotham_peer_connector.h"
#include <memory>
#include <functional>
#include <vector>
#include <string>

/**
 * @brief High-level wrapper for Gotham's private Tor mesh network
 * 
 * This class combines TorService, TorOnionIdentityManager, and GothamPeerConnector
 * to provide a complete private mesh networking solution.
 */
class GothamTorMesh {
public:
    /**
     * @brief Construct a new Gotham Tor Mesh
     * 
     * @param data_directory Base directory for storing mesh data
     */
    GothamTorMesh(const std::string& data_directory = "/tmp/gotham_mesh");
    
    /**
     * @brief Destroy the Gotham Tor Mesh
     */
    ~GothamTorMesh();
    
    // Mesh management:
    
    /**
     * @brief Start the mesh network
     * 
     * @param socks_port SOCKS proxy port (default: 9050)
     * @param control_port Tor control port (default: 9051)
     * @param p2p_port P2P communication port (default: 12345)
     * @return true if started successfully, false otherwise
     */
    bool start(int socks_port = 9050, int control_port = 9051, int p2p_port = 12345);
    
    /**
     * @brief Stop the mesh network
     */
    void stop();
    
    /**
     * @brief Check if the mesh is running
     * 
     * @return true if running, false otherwise
     */
    bool isRunning() const;
    
    // Identity management:
    
    /**
     * @brief Get this node's .onion address
     * 
     * @return std::string The .onion address, or empty string if not available
     */
    std::string getMyOnionAddress();
    
    /**
     * @brief Add a trusted peer to the network
     * 
     * @param onion_address The peer's .onion address
     * @return true if added successfully, false otherwise
     */
    bool addTrustedPeer(const std::string& onion_address);
    
    /**
     * @brief Get list of trusted peers
     * 
     * @return std::vector<std::string> List of trusted .onion addresses
     */
    std::vector<std::string> getTrustedPeers();
    
    /**
     * @brief Remove a trusted peer
     * 
     * @param onion_address Peer's .onion address to remove
     * @return true if removed successfully, false otherwise
     */
    bool removeTrustedPeer(const std::string& onion_address);
    
    // Messaging:
    
    /**
     * @brief Send a message to a specific peer
     * 
     * @param peer_address The peer's .onion address
     * @param message The message to send
     * @return true if sent successfully, false otherwise
     */
    bool sendMessage(const std::string& peer_address, const std::string& message);
    
    /**
     * @brief Broadcast a message to all connected peers
     * 
     * @param message The message to broadcast
     * @return true if sent to at least one peer, false otherwise
     */
    bool broadcastMessage(const std::string& message);
    
    // Event callbacks:
    
    /**
     * @brief Set handler for incoming messages
     * 
     * @param handler Function to call when messages are received
     */
    void setMessageHandler(std::function<void(const std::string&, const std::string&)> handler);
    
    /**
     * @brief Set handler for peer connection events
     * 
     * @param handler Function to call when peers connect/disconnect
     */
    void setPeerConnectionHandler(std::function<void(const std::string&, bool)> handler);
    
    // Network status:
    
    /**
     * @brief Get number of currently connected peers
     * 
     * @return int Number of connected peers
     */
    int getConnectedPeerCount();
    
    /**
     * @brief Get list of currently connected peers
     * 
     * @return std::vector<std::string> List of connected peer addresses
     */
    std::vector<std::string> getConnectedPeers();
    
    /**
     * @brief Get detailed information about connected peers
     * 
     * @return std::vector<GothamPeerConnector::PeerInfo> Peer information
     */
    std::vector<GothamPeerConnector::PeerInfo> getConnectedPeersInfo();
    
    /**
     * @brief Get the peer connector for direct DHT integration
     * 
     * @return GothamPeerConnector* Peer connector instance
     */
    GothamPeerConnector* getPeerConnector();
    
    // Advanced operations:
    
    /**
     * @brief Connect to all known trusted peers
     * 
     * @return int Number of successful connections
     */
    int connectToAllTrustedPeers();
    
    /**
     * @brief Export this node's identity for sharing
     * 
     * @param export_path Path to export identity to
     * @return true if exported successfully, false otherwise
     */
    bool exportMyIdentity(const std::string& export_path);
    
    /**
     * @brief Import a peer's identity (for trusted peer list)
     * 
     * @param import_path Path to import identity from
     * @param service_name Name to give this identity
     * @return true if imported successfully, false otherwise
     */
    bool importPeerIdentity(const std::string& import_path, const std::string& service_name);
    
    // Dynamic Privacy Mode:
    
    /**
     * @brief Enable dynamic privacy mode (completely fresh .onion every session)
     * 
     * In this mode:
     * 1. Generate completely fresh .onion address every startup
     * 2. No fixed addresses or persistent identities
     * 3. Bootstrap through seed servers for peer discovery
     * 4. Maximum privacy protection
     * 
     * @param seed_servers List of seed server .onion addresses for bootstrapping
     * @return true if enabled successfully
     */
    bool enableDynamicPrivacyMode(const std::vector<std::string>& seed_servers);
    
    /**
     * @brief Check if dynamic privacy mode is enabled
     * 
     * @return true if dynamic privacy mode is active
     */
    bool isDynamicPrivacyEnabled() const;
    
    /**
     * @brief Bootstrap from seed servers to discover peers
     * 
     * @return Number of peers discovered and added
     */
    int bootstrapFromSeeds();
    
    /**
     * @brief Register this node with seed servers
     * 
     * @return true if registered with at least one seed
     */
    bool registerWithSeeds();
    
    /**
     * @brief Get list of configured seed servers
     * 
     * @return std::vector<std::string> List of seed server addresses
     */
    std::vector<std::string> getSeedServers() const;
    

    
    /**
     * @brief Get network statistics
     * 
     * @return std::string Human-readable network statistics
     */
    std::string getNetworkStats();

private:
    std::string data_directory_;
    std::unique_ptr<TorService> tor_service_;
    std::unique_ptr<TorOnionIdentityManager> identity_manager_;
    std::unique_ptr<GothamPeerConnector> peer_connector_;
    
    bool running_;
    int socks_port_;
    int control_port_;
    int p2p_port_;
    
    // Default trusted peers (can be configured)
    std::vector<std::string> default_bootstrap_peers_;
    
    // Dynamic privacy mode
    bool dynamic_privacy_enabled_;
    std::vector<std::string> seed_servers_;
    std::string current_session_id_;  // Unique session identifier
    
    /**
     * @brief Initialize default/bootstrap peers
     */
    void initializeDefaultPeers();
    
    /**
     * @brief Start peer discovery process
     */
    void startPeerDiscovery();
    
    /**
     * @brief Wait for Tor to be ready and onion service to be generated
     * 
     * @param timeout_seconds Maximum time to wait
     * @return true if ready, false if timeout
     */
    bool waitForTorReady(int timeout_seconds = 30);
    
    /**
     * @brief Internal message handler that can be extended
     * 
     * @param from_peer Sender's address
     * @param message Message content
     */
    void internalMessageHandler(const std::string& from_peer, const std::string& message);
    
    /**
     * @brief Internal connection handler that can be extended
     * 
     * @param peer_address Peer's address
     * @param connected Connection status
     */
    void internalConnectionHandler(const std::string& peer_address, bool connected);
    
    /**
     * @brief Generate unique session ID for dynamic privacy mode
     * 
     * @return std::string Unique session identifier
     */
    std::string generateSessionId();
    
    // User-defined handlers
    std::function<void(const std::string&, const std::string&)> user_message_handler_;
    std::function<void(const std::string&, bool)> user_connection_handler_;
};