#pragma once

#include <string>
#include <vector>
#include <optional>

/**
 * @brief Manages Tor onion service identities and keys
 * 
 * This class handles the creation, management, and discovery of
 * .onion service identities for the Gotham mesh network.
 */
class TorOnionIdentityManager {
public:
    struct OnionIdentity {
        std::string onion_address;      // e.g., "abcd123...onion"
        std::string private_key_path;   // Path to private key file
        std::string public_key_path;    // Path to public key file
        int service_port;               // Service port (e.g., 12345)
        int local_port;                 // Local port mapping
        std::string service_name;       // Service identifier
    };
    
    /**
     * @brief Construct a new Tor Onion Identity Manager
     * 
     * @param data_directory Base directory for storing onion service data
     */
    TorOnionIdentityManager(const std::string& data_directory);
    
    /**
     * @brief Destroy the Tor Onion Identity Manager
     */
    ~TorOnionIdentityManager();
    
    // Identity management:
    
    /**
     * @brief Create a new onion service identity
     * 
     * @param service_name Unique name for this service
     * @param service_port Port exposed by the hidden service
     * @param local_port Local port to map to
     * @return true if created successfully, false otherwise
     */
    bool createIdentity(const std::string& service_name, int service_port, int local_port);
    
    /**
     * @brief Get identity information for a service
     * 
     * @param service_name Name of the service
     * @return OnionIdentity if found, nullopt otherwise
     */
    std::optional<OnionIdentity> getIdentity(const std::string& service_name);
    
    /**
     * @brief Get all managed identities
     * 
     * @return std::vector<OnionIdentity> List of all identities
     */
    std::vector<OnionIdentity> getAllIdentities();
    
    /**
     * @brief Delete an identity and its associated files
     * 
     * @param service_name Name of the service to delete
     * @return true if deleted successfully, false otherwise
     */
    bool deleteIdentity(const std::string& service_name);
    
    // Key management:
    
    /**
     * @brief Export identity keys to a file
     * 
     * @param service_name Service to export
     * @param export_path Path to export to
     * @return true if exported successfully, false otherwise
     */
    bool exportIdentity(const std::string& service_name, const std::string& export_path);
    
    /**
     * @brief Import identity keys from a file
     * 
     * @param service_name Service name to import as
     * @param import_path Path to import from
     * @return true if imported successfully, false otherwise
     */
    bool importIdentity(const std::string& service_name, const std::string& import_path);
    
    // Address resolution:
    
    /**
     * @brief Get the .onion address for a service
     * 
     * @param service_name Service name
     * @return std::string The .onion address, or empty string if not found
     */
    std::string getOnionAddress(const std::string& service_name);
    
    /**
     * @brief Validate if an address is a proper v3 onion address
     * 
     * @param address Address to validate
     * @return true if valid, false otherwise
     */
    static bool isValidOnionAddress(const std::string& address);

private:
    std::string data_directory_;
    
    /**
     * @brief Get the directory path for a service
     * 
     * @param service_name Name of the service
     * @return std::string Directory path
     */
    std::string getServiceDirectory(const std::string& service_name);
    
    /**
     * @brief Read the hostname file to get the onion address
     * 
     * @param service_dir Service directory
     * @param onion_address Output parameter for the address
     * @return true if read successfully, false otherwise
     */
    bool readHostnameFile(const std::string& service_dir, std::string& onion_address);
    
    /**
     * @brief Ensure the base directory structure exists
     */
    void ensureDirectoryStructure();
};