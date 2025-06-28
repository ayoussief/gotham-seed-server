#pragma once

#include <string>
#include <thread>
#include <atomic>

// Forward declarations to avoid including Tor headers in public interface
struct tor_main_configuration_t;

/**
 * @brief A clean wrapper around Tor functionality
 * 
 * This class provides a simple interface to start, stop, and manage
 * an embedded Tor instance within the application.
 */
class TorService {
public:
    /**
     * @brief Construct a new Tor Service object
     */
    TorService();
    
    /**
     * @brief Destroy the Tor Service object
     */
    ~TorService();
    
    /**
     * @brief Start the Tor service with specified ports
     * 
     * @param socks_port SOCKS proxy port (default: 9050)
     * @param control_port Control port (default: 9051)
     * @param data_directory Directory for Tor data (default: /tmp/gotham_tor_data)
     * @return true if started successfully, false otherwise
     */
    bool start(int socks_port = 9050, int control_port = 9051, 
               const std::string& data_directory = "/tmp/gotham_tor_data");
    
    /**
     * @brief Stop the Tor service gracefully
     */
    void stop();
    
    /**
     * @brief Check if Tor service is running
     * 
     * @return true if running, false otherwise
     */
    bool isRunning() const;
    
    /**
     * @brief Wait for Tor service to exit
     */
    void waitForExit();
    
    /**
     * @brief Get the SOCKS proxy port
     * 
     * @return int SOCKS port number, or -1 if not started
     */
    int getSocksPort() const;
    
    /**
     * @brief Get the control port
     * 
     * @return int Control port number, or -1 if not started
     */
    int getControlPort() const;
    
    /**
     * @brief Get Tor API version string
     * 
     * @return std::string Version information
     */
    static std::string getVersion();
    
    /**
     * @brief Get the onion address for this node
     * 
     * @return std::string The .onion address, or empty string if not available
     */
    std::string getOnionAddress() const;
    
    /**
     * @brief Create a new hidden service via control port
     * 
     * @param service_name Name for the new service (used for directory)
     * @param port Port number for the service
     * @return std::string The new .onion address, or empty string if failed
     */
    std::string createNewHiddenService(const std::string& service_name, int port = 12345);

private:
    // Private implementation details
    tor_main_configuration_t* config_;
    std::thread tor_thread_;
    std::atomic<bool> running_;
    int socks_port_;
    int control_port_;
    std::string data_directory_;
    
    // Non-copyable
    TorService(const TorService&) = delete;
    TorService& operator=(const TorService&) = delete;
};