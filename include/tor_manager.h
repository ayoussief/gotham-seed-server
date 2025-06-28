#pragma once

#include <string>
#include <memory>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <atomic>
#include "../src/tor-wrapper/include/tor_service.h"

/**
 * @brief Manages Tor service for the seed server
 * 
 * Handles Tor initialization, hidden service creation, and connection management.
 * Uses the stable tor-wrapper instead of embedded Tor.
 */
class TorManager {
public:
    using ConnectionHandler = std::function<void(int socket_fd, const std::string& peer_address)>;
    
    /**
     * @brief Construct a new Tor Manager
     * 
     * @param data_directory Directory for Tor configuration and data
     * @param port Port to listen on for incoming connections
     */
    TorManager(const std::string& data_directory, int port);
    
    /**
     * @brief Destroy the Tor Manager
     */
    ~TorManager();
    
    /**
     * @brief Start embedded Tor service and hidden service
     * 
     * @return true if started successfully, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop embedded Tor service
     */
    void stop();
    
    /**
     * @brief Check if embedded Tor is running
     * 
     * @return true if running, false otherwise
     */
    bool isRunning() const;
    
    /**
     * @brief Get the hidden service .onion address
     * 
     * @return std::string The .onion address, empty if not available
     */
    std::string getOnionAddress() const;
    
    /**
     * @brief Set connection handler for incoming connections
     * 
     * @param handler Function to call when new connections arrive
     */
    void setConnectionHandler(ConnectionHandler handler);
    
    /**
     * @brief Start listening for incoming connections
     * 
     * @return true if listening started successfully
     */
    bool startListening();
    
    /**
     * @brief Stop listening for incoming connections
     */
    void stopListening();
    
    /**
     * @brief Get embedded Tor API version
     * 
     * @return std::string Version information
     */
    static std::string getVersion();

private:
    std::unique_ptr<TorService> tor_service_;
    std::string data_directory_;
    int port_;
    std::atomic<bool> listening_;
    int listen_socket_;
    std::thread listen_thread_;
    ConnectionHandler connection_handler_;
    
    bool createListenSocket();
    void listenLoop();
    void handleConnection(int client_socket);
    std::string getPeerAddress(int socket_fd);
};