#include "onion_identity_manager.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <iostream>

TorOnionIdentityManager::TorOnionIdentityManager(const std::string& data_directory)
    : data_directory_(data_directory) {
    ensureDirectoryStructure();
}

TorOnionIdentityManager::~TorOnionIdentityManager() = default;

bool TorOnionIdentityManager::createIdentity(const std::string& service_name, 
                                           int service_port, int local_port) {
    std::string service_dir = getServiceDirectory(service_name);
    
    try {
        // Create service directory
        if (!std::filesystem::create_directories(service_dir)) {
            // Directory might already exist, check if it's actually a directory
            if (!std::filesystem::is_directory(service_dir)) {
                std::cerr << "Failed to create service directory: " << service_dir << std::endl;
                return false;
            }
        }
        
        // Tor will automatically generate keys when it starts
        // For now, we just ensure the directory structure exists
        std::cout << "Created identity directory for service: " << service_name << std::endl;
        return true;
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Filesystem error creating identity: " << e.what() << std::endl;
        return false;
    }
}

std::optional<TorOnionIdentityManager::OnionIdentity> 
TorOnionIdentityManager::getIdentity(const std::string& service_name) {
    std::string service_dir = getServiceDirectory(service_name);
    std::string hostname_file = service_dir + "/hostname";
    
    if (!std::filesystem::exists(hostname_file)) {
        return std::nullopt;
    }
    
    OnionIdentity identity;
    identity.service_name = service_name;
    
    if (!readHostnameFile(service_dir, identity.onion_address)) {
        return std::nullopt;
    }
    
    identity.private_key_path = service_dir + "/hs_ed25519_secret_key";
    identity.public_key_path = service_dir + "/hs_ed25519_public_key";
    identity.service_port = 12345; // Default port - could be made configurable
    identity.local_port = 12345;   // Default port - could be made configurable
    
    return identity;
}

std::vector<TorOnionIdentityManager::OnionIdentity> 
TorOnionIdentityManager::getAllIdentities() {
    std::vector<OnionIdentity> identities;
    std::string services_dir = data_directory_ + "/services";
    
    try {
        if (!std::filesystem::exists(services_dir)) {
            return identities;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(services_dir)) {
            if (entry.is_directory()) {
                std::string service_name = entry.path().filename().string();
                auto identity = getIdentity(service_name);
                if (identity.has_value()) {
                    identities.push_back(identity.value());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error listing identities: " << e.what() << std::endl;
    }
    
    return identities;
}

bool TorOnionIdentityManager::deleteIdentity(const std::string& service_name) {
    std::string service_dir = getServiceDirectory(service_name);
    
    try {
        if (std::filesystem::exists(service_dir)) {
            std::filesystem::remove_all(service_dir);
            std::cout << "Deleted identity: " << service_name << std::endl;
            return true;
        }
        return false;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error deleting identity: " << e.what() << std::endl;
        return false;
    }
}

bool TorOnionIdentityManager::exportIdentity(const std::string& service_name, 
                                           const std::string& export_path) {
    std::string service_dir = getServiceDirectory(service_name);
    
    try {
        if (!std::filesystem::exists(service_dir)) {
            std::cerr << "Service directory does not exist: " << service_dir << std::endl;
            return false;
        }
        
        std::filesystem::copy(service_dir, export_path, 
                            std::filesystem::copy_options::recursive);
        std::cout << "Exported identity " << service_name << " to " << export_path << std::endl;
        return true;
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error exporting identity: " << e.what() << std::endl;
        return false;
    }
}

bool TorOnionIdentityManager::importIdentity(const std::string& service_name, 
                                           const std::string& import_path) {
    std::string service_dir = getServiceDirectory(service_name);
    
    try {
        if (!std::filesystem::exists(import_path)) {
            std::cerr << "Import path does not exist: " << import_path << std::endl;
            return false;
        }
        
        // Remove existing service directory if it exists
        if (std::filesystem::exists(service_dir)) {
            std::filesystem::remove_all(service_dir);
        }
        
        std::filesystem::copy(import_path, service_dir, 
                            std::filesystem::copy_options::recursive);
        std::cout << "Imported identity from " << import_path << " as " << service_name << std::endl;
        return true;
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error importing identity: " << e.what() << std::endl;
        return false;
    }
}

std::string TorOnionIdentityManager::getOnionAddress(const std::string& service_name) {
    auto identity = getIdentity(service_name);
    return identity.has_value() ? identity->onion_address : "";
}

bool TorOnionIdentityManager::isValidOnionAddress(const std::string& address) {
    // v3 onion addresses are 56 characters + ".onion"
    std::regex onion_regex(R"([a-z2-7]{56}\.onion)");
    return std::regex_match(address, onion_regex);
}

std::string TorOnionIdentityManager::getServiceDirectory(const std::string& service_name) {
    return data_directory_ + "/services/" + service_name;
}

bool TorOnionIdentityManager::readHostnameFile(const std::string& service_dir, 
                                             std::string& onion_address) {
    std::ifstream file(service_dir + "/hostname");
    if (!file.is_open()) {
        return false;
    }
    
    std::getline(file, onion_address);
    
    // Remove any trailing whitespace/newlines
    onion_address.erase(onion_address.find_last_not_of(" \n\r\t") + 1);
    
    return !onion_address.empty();
}

void TorOnionIdentityManager::ensureDirectoryStructure() {
    try {
        std::filesystem::create_directories(data_directory_);
        std::filesystem::create_directories(data_directory_ + "/services");
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error creating directory structure: " << e.what() << std::endl;
    }
}