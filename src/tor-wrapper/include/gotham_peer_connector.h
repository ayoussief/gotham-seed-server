#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include "gotham_protocol.h"

/**
 * @brief Handles P2P connections through SOCKS5 to .onion addresses
 * 
 * This class manages connections to other Gotham nodes through the Tor
 * SOCKS proxy, providing a high-level interface for peer communication.
 */
class GothamPeerConnector {
public:
    struct PeerInfo {
        std::string onion_address;
        int port;
        std::string node_id;        // Public key identifier
        bool is_connected;
        uint64_t last_seen;
        int socket_fd;              // Socket file descriptor
    };
    
    using MessageHandler = std::function<void(const std::string& from_peer, const std::string& message)>;
    using ConnectionHandler = std::function<void(const std::string& peer_address, bool connected)>;
    
    /**
     * @brief Construct a new Gotham Peer Connector
     * 
     * @param socks_proxy_host SOCKS proxy host (default: "127.0.0.1")
     * @param socks_proxy_port SOCKS proxy port (default: 9050)
     */
    GothamPeerConnector(const std::string& socks_proxy_host = "127.0.0.1", int socks_proxy_port = 9050);
    
    /**
     * @brief Destroy the Gotham Peer Connector
     */
    ~GothamPeerConnector();
    
    // Connection management:
    
    /**
     * @brief Connect to a peer via their .onion address
     * 
     * @param onion_address The peer's .onion address
     * @param port The port to connect to
     * @return true if connection initiated successfully, false otherwise
     */
    bool connectToPeer(const std::string& onion_address, int port);
    
    /**
     * @brief Disconnect from a peer
     * 
     * @param onion_address The peer's .onion address
     * @return true if disconnected successfully, false otherwise
     */
    bool disconnectFromPeer(const std::string& onion_address);
    
    /**
     * @brief Get list of currently connected peers
     * 
     * @return std::vector<PeerInfo> List of connected peers
     */
    std::vector<PeerInfo> getConnectedPeers();
    
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
    
    // Event handlers:
    
    /**
     * @brief Set handler for incoming messages
     * 
     * @param handler Function to call when messages are received
     */
    void setMessageHandler(MessageHandler handler);
    
    /**
     * @brief Set handler for connection events
     * 
     * @param handler Function to call when peers connect/disconnect
     */
    void setConnectionHandler(ConnectionHandler handler);
    
    // Peer discovery:
    
    /**
     * @brief Add a known peer to the list
     * 
     * @param onion_address The peer's .onion address
     * @param port The peer's port
     * @return true if added successfully, false otherwise
     */
    bool addKnownPeer(const std::string& onion_address, int port);
    
    /**
     * @brief Remove a peer from the known peers list
     * 
     * @param onion_address The peer's .onion address
     * @return true if removed successfully, false otherwise
     */
    bool removeKnownPeer(const std::string& onion_address);
    
    /**
     * @brief Get list of known peer addresses
     * 
     * @return std::vector<std::string> List of known .onion addresses
     */
    std::vector<std::string> getKnownPeers();
    
    // Network operations:
    
    /**
     * @brief Start listening for incoming connections
     * 
     * @param local_port Port to listen on
     */
    void startListening(int local_port);
    
    /**
     * @brief Stop listening for incoming connections
     */
    void stopListening();
    
    /**
     * @brief Check if currently listening for connections
     * 
     * @return true if listening, false otherwise
     */
    bool isListening() const;

private:
    std::string socks_host_;
    int socks_port_;
    std::atomic<bool> listening_;
    std::atomic<bool> running_;
    int local_port_;
    int listen_socket_;
    
    std::map<std::string, PeerInfo> connected_peers_;
    std::vector<std::string> known_peers_;
    
    MessageHandler message_handler_;
    ConnectionHandler connection_handler_;
    
    std::thread listen_thread_;
    std::vector<std::thread> connection_threads_;
    std::mutex peers_mutex_;
    std::mutex known_peers_mutex_;
    
    // SOCKS5 implementation:
    
    /**
     * @brief Create a connection through SOCKS5 proxy
     * 
     * @param target_host Target hostname (.onion address)
     * @param target_port Target port
     * @return int Socket file descriptor, or -1 on failure
     */
    int createSocksConnection(const std::string& target_host, int target_port);
    
    /**
     * @brief Perform SOCKS5 handshake
     * 
     * @param socket Socket file descriptor
     * @param target_host Target hostname
     * @param target_port Target port
     * @return true if handshake successful, false otherwise
     */
    bool performSocksHandshake(int socket, const std::string& target_host, int target_port);
    
    // Protocol implementation:
    
    /**
     * @brief Perform Gotham-specific handshake with peer
     * 
     * @param socket Socket file descriptor
     * @param peer_address Peer's .onion address
     * @return true if handshake successful, false otherwise
     */
    bool performGothamHandshake(int socket, const std::string& peer_address);
    
    /**
     * @brief Handle an incoming connection
     * 
     * @param client_socket Client socket file descriptor
     */
    void handleIncomingConnection(int client_socket);
    
    /**
     * @brief Handle an incoming message
     * 
     * @param from_peer Sender's address
     * @param message The message content
     */
    void handleIncomingMessage(const std::string& from_peer, const std::string& message);
    
    /**
     * @brief Listen for incoming connections (runs in separate thread)
     */
    void listenLoop();
    
    /**
     * @brief Handle communication with a connected peer (runs in separate thread)
     * 
     * @param peer_address Peer's .onion address
     * @param socket Socket file descriptor
     */
    void handlePeerCommunication(const std::string& peer_address, int socket);
    
    /**
     * @brief Clean up disconnected peers
     */
    void cleanupPeers();
    
    /**
     * @brief Get current timestamp in milliseconds
     * 
     * @return uint64_t Current timestamp
     */
    uint64_t getCurrentTimestamp();
};