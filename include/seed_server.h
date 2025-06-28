#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

class PeerManager;
class GCTYHandler;
class TorManager;

/**
 * @brief Main Gotham City Seed Server class
 * 
 * Coordinates all seed server components and manages the main server loop.
 */
class SeedServer {
public:
    struct Config {
        int port = 12345;
        int max_peers = 500;
        int cleanup_interval_seconds = 180;
        int rate_limit_per_minute = 60;
        std::string data_directory = "";
        bool verbose = false;
        
        Config() {
            // Set default data directory
            const char* home = getenv("HOME");
            data_directory = home ? std::string(home) + "/.gotham-seed" : "/tmp/gotham-seed";
        }
    };

    /**
     * @brief Construct a new Seed Server
     * 
     * @param config Server configuration
     */
    explicit SeedServer(const Config& config);
    
    /**
     * @brief Destroy the Seed Server
     */
    ~SeedServer();
    
    /**
     * @brief Start the seed server
     * 
     * @return true if started successfully, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop the seed server
     */
    void stop();
    
    /**
     * @brief Check if server is running
     * 
     * @return true if running, false otherwise
     */
    bool isRunning() const;
    
    /**
     * @brief Get server statistics
     * 
     * @return std::string Statistics in human-readable format
     */
    std::string getStats() const;
    
    /**
     * @brief Get server's .onion address
     * 
     * @return std::string The .onion address, empty if not available
     */
    std::string getOnionAddress() const;

private:
    Config config_;
    std::atomic<bool> running_;
    std::atomic<bool> shutdown_requested_;
    
    // Core components
    std::unique_ptr<TorManager> tor_manager_;
    std::unique_ptr<PeerManager> peer_manager_;
    std::unique_ptr<GCTYHandler> gcty_handler_;
    
    // Background threads
    std::thread server_thread_;
    std::thread cleanup_thread_;
    
    /**
     * @brief Main server loop
     */
    void serverLoop();
    
    /**
     * @brief Cleanup loop for removing inactive peers
     */
    void cleanupLoop();
    
    /**
     * @brief Initialize all components
     * 
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * @brief Cleanup all components
     */
    void cleanup();
    
    /**
     * @brief Handle incoming connection
     * 
     * @param socket_fd Socket file descriptor
     * @param peer_address Peer address identifier
     */
    void handleConnection(int socket_fd, const std::string& peer_address);
    
    /**
     * @brief Log message with timestamp
     * 
     * @param level Log level (INFO, WARN, ERROR)
     * @param message Message to log
     */
    void log(const std::string& level, const std::string& message) const;
};