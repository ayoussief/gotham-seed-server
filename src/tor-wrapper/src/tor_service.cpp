#include "tor_service.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Include Tor API
extern "C" {
#include "feature/api/tor_api.h"
}

TorService::TorService() 
    : config_(nullptr), running_(false), socks_port_(-1), control_port_(-1) {
}

TorService::~TorService() {
    try {
        stop();
    } catch (...) {
        // Ignore exceptions in destructor
    }
    
    try {
        if (config_) {
            tor_main_configuration_free(config_);
        }
    } catch (...) {
        // Ignore exceptions in destructor
    }
}

bool TorService::start(int socks_port, int control_port, const std::string& data_directory) {
    if (running_.load()) {
        std::cout << "Tor is already running!" << std::endl;
        return false;
    }
    
    config_ = tor_main_configuration_new();
    if (!config_) {
        std::cerr << "Failed to create Tor configuration" << std::endl;
        return false;
    }
    
    // Store configuration
    socks_port_ = socks_port;
    control_port_ = control_port;
    data_directory_ = data_directory;
    
    // Prepare command line arguments for Tor
    std::vector<std::string> args = {
        "tor",
        "--SocksPort", std::to_string(socks_port),
        "--ControlPort", std::to_string(control_port),
        "--DataDirectory", data_directory,
        "--Log", "notice stdout",
        "--DisableDebuggerAttachment", "0",
        
        // Process management - Tor will exit if our process dies
        "--__OwningControllerProcess", std::to_string(getpid()),
        
        // Control port authentication - use cookie authentication
        "--CookieAuthentication", "1",              // Enable cookie auth
        "--CookieAuthFile", data_directory + "/control_auth_cookie",
        
        // Gotham private mesh configuration:
        "--ClientOnly", "1",                        // Client only mode
        "--ExitRelay", "0",                         // No exit to internet
        "--ExitPolicy", "reject *:*",               // Block all exit traffic
        "--PublishServerDescriptor", "0",           // Don't publish to public directories
        
        // Hidden service configuration:
        "--HiddenServiceDir", data_directory + "/gotham_hs",
        "--HiddenServicePort", "12345 127.0.0.1:12345"
    };
    
    // Convert to char* array
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    
    if (tor_main_configuration_set_command_line(config_, argv.size(), argv.data()) < 0) {
        std::cerr << "Failed to set Tor command line" << std::endl;
        return false;
    }
    
    std::cout << "Starting Tor service..." << std::endl;
    std::cout << "SOCKS proxy will be available on: 127.0.0.1:" << socks_port << std::endl;
    std::cout << "Control port will be available on: 127.0.0.1:" << control_port << std::endl;
    
    // Start Tor in a separate thread with proper shutdown handling
    running_.store(true);
    tor_thread_ = std::thread([this]() {
        try {
            int result = tor_run_main(this->config_);
            if (result != 0 && this->running_.load()) {
                std::cerr << "Tor exited with error code: " << result << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Tor thread exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown Tor thread exception" << std::endl;
        }
        this->running_.store(false);
        std::cout << "🧅 Tor main loop exited" << std::endl;
    });
    
    // Give Tor some time to start
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    return true;
}

void TorService::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "Stopping Tor service..." << std::endl;
    
    // Try to shutdown Tor gracefully via control port with authentication
    try {
        int control_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (control_sock >= 0) {
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(control_port_);
            addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            
            if (connect(control_sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                // Authenticate first
                std::string cookie_file = data_directory_ + "/control_auth_cookie";
                std::ifstream cookie_stream(cookie_file, std::ios::binary);
                if (cookie_stream.is_open()) {
                    std::vector<char> cookie_data((std::istreambuf_iterator<char>(cookie_stream)),
                                                  std::istreambuf_iterator<char>());
                    cookie_stream.close();
                    
                    if (!cookie_data.empty()) {
                        // Convert cookie to hex
                        std::string cookie_hex;
                        for (unsigned char byte : cookie_data) {
                            char hex_chars[3];
                            sprintf(hex_chars, "%02X", byte);
                            cookie_hex += hex_chars;
                        }
                        
                        // Send auth command
                        std::string auth_cmd = "AUTHENTICATE " + cookie_hex + "\r\n";
                        send(control_sock, auth_cmd.c_str(), auth_cmd.length(), 0);
                        
                        // Read auth response (with timeout)
                        char auth_buffer[256];
                        ssize_t auth_bytes = recv(control_sock, auth_buffer, sizeof(auth_buffer) - 1, 0);
                        if (auth_bytes > 0) {
                            auth_buffer[auth_bytes] = '\0';
                            if (std::string(auth_buffer).find("250 OK") != std::string::npos) {
                                // Send shutdown command
                                std::string shutdown_cmd = "SIGNAL SHUTDOWN\r\n";
                                send(control_sock, shutdown_cmd.c_str(), shutdown_cmd.length(), 0);
                                std::cout << "✅ Sent authenticated SHUTDOWN signal to Tor" << std::endl;
                            }
                        }
                    }
                }
            }
            close(control_sock);
        }
    } catch (...) {
        std::cout << "⚠️ Failed to send graceful shutdown to Tor" << std::endl;
    }
    
    // Wait for graceful shutdown with timeout
    running_.store(false);
    
    if (tor_thread_.joinable()) {
        // Give Tor some time to shutdown gracefully
        std::cout << "⏳ Waiting for Tor to shutdown gracefully..." << std::endl;
        
        auto start_time = std::chrono::steady_clock::now();
        bool tor_finished = false;
        
        // Wait up to 3 seconds for graceful shutdown (reduced timeout)
        while (std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - start_time).count() < 3) {
            
            // Check if thread finished naturally
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            // Try to join with timeout
            if (tor_thread_.joinable()) {
                // Still running, continue waiting
                continue;
            } else {
                tor_finished = true;
                break;
            }
        }
        
        if (tor_thread_.joinable()) {
            if (tor_finished) {
                tor_thread_.join();
                std::cout << "✅ Tor shutdown gracefully" << std::endl;
            } else {
                std::cout << "⚠️ Tor shutdown timeout - force detaching..." << std::endl;
                tor_thread_.detach();
                
                // As last resort, try to kill any remaining tor processes
                std::cout << "🔪 Force terminating Tor processes..." << std::endl;
                
                // Try multiple approaches to ensure cleanup
                system("pkill -f 'tor.*DataDirectory' 2>/dev/null || true");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                system("pkill -9 -f 'tor.*DataDirectory' 2>/dev/null || true");
                
                std::cout << "⚡ Force termination complete" << std::endl;
            }
        }
    }
    
    std::cout << "✅ Tor service stopped" << std::endl;
}

bool TorService::isRunning() const {
    return running_.load();
}

void TorService::waitForExit() {
    if (tor_thread_.joinable()) {
        tor_thread_.join();
    }
}

int TorService::getSocksPort() const {
    return isRunning() ? socks_port_ : -1;
}

int TorService::getControlPort() const {
    return isRunning() ? control_port_ : -1;
}

std::string TorService::getVersion() {
    const char* version = tor_api_get_provider_version();
    return version ? std::string(version) : "Unknown";
}

std::string TorService::getOnionAddress() const {
    if (!isRunning()) {
        return "";
    }
    
    std::string hostname_file = data_directory_ + "/gotham_hs/hostname";
    std::ifstream file(hostname_file);
    
    if (!file.is_open()) {
        return "";
    }
    
    std::string onion_address;
    std::getline(file, onion_address);
    
    // Remove any trailing whitespace/newlines
    onion_address.erase(onion_address.find_last_not_of(" \n\r\t") + 1);
    
    return onion_address;
}

std::string TorService::createNewHiddenService(const std::string& service_name, int port) {
    if (!isRunning()) {
        std::cerr << "❌ Tor service not running" << std::endl;
        return "";
    }
    
    std::cout << "🔧 Creating new hidden service: " << service_name << std::endl;
    
    try {
        // Connect to Tor control port
        int control_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (control_sock < 0) {
            std::cerr << "❌ Failed to create control socket" << std::endl;
            return "";
        }
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(control_port_);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        
        if (connect(control_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            std::cerr << "❌ Failed to connect to Tor control port" << std::endl;
            close(control_sock);
            return "";
        }
        
        // Authenticate using cookie file
        std::string cookie_file = data_directory_ + "/control_auth_cookie";
        std::ifstream cookie_stream(cookie_file, std::ios::binary);
        if (!cookie_stream.is_open()) {
            std::cerr << "❌ Failed to open cookie file: " << cookie_file << std::endl;
            close(control_sock);
            return "";
        }
        
        // Read cookie data
        std::vector<char> cookie_data((std::istreambuf_iterator<char>(cookie_stream)),
                                      std::istreambuf_iterator<char>());
        cookie_stream.close();
        
        if (cookie_data.empty()) {
            std::cerr << "❌ Cookie file is empty" << std::endl;
            close(control_sock);
            return "";
        }
        
        // Convert cookie to hex string
        std::string cookie_hex;
        for (unsigned char byte : cookie_data) {
            char hex_chars[3];
            sprintf(hex_chars, "%02X", byte);
            cookie_hex += hex_chars;
        }
        
        // Send authentication command
        std::string auth_cmd = "AUTHENTICATE " + cookie_hex + "\r\n";
        if (send(control_sock, auth_cmd.c_str(), auth_cmd.length(), 0) < 0) {
            std::cerr << "❌ Failed to send authentication command" << std::endl;
            close(control_sock);
            return "";
        }
        
        // Read authentication response
        char auth_buffer[256];
        ssize_t auth_bytes = recv(control_sock, auth_buffer, sizeof(auth_buffer) - 1, 0);
        if (auth_bytes <= 0) {
            std::cerr << "❌ No authentication response" << std::endl;
            close(control_sock);
            return "";
        }
        
        auth_buffer[auth_bytes] = '\0';
        std::string auth_response(auth_buffer);
        
        if (auth_response.find("250 OK") == std::string::npos) {
            std::cerr << "❌ Authentication failed: " << auth_response << std::endl;
            close(control_sock);
            return "";
        }
        
        std::cout << "✅ Authenticated with Tor control port" << std::endl;
        
        // Send ADD_ONION command to create new hidden service
        std::string add_onion_cmd = "ADD_ONION NEW:ED25519-V3 Port=" + std::to_string(port) + ",127.0.0.1:" + std::to_string(port) + "\r\n";
        
        if (send(control_sock, add_onion_cmd.c_str(), add_onion_cmd.length(), 0) < 0) {
            std::cerr << "❌ Failed to send ADD_ONION command" << std::endl;
            close(control_sock);
            return "";
        }
        
        // Read response
        char buffer[1024];
        ssize_t bytes_received = recv(control_sock, buffer, sizeof(buffer) - 1, 0);
        close(control_sock);
        
        if (bytes_received <= 0) {
            std::cerr << "❌ No response from Tor control port" << std::endl;
            return "";
        }
        
        buffer[bytes_received] = '\0';
        std::string response(buffer);
        
        std::cout << "🔧 Tor control response: " << response << std::endl;
        
        // Parse response to extract .onion address
        // Response format: "250-ServiceID=<onion_address_without_.onion>"
        size_t service_id_pos = response.find("ServiceID=");
        if (service_id_pos != std::string::npos) {
            size_t start = service_id_pos + 10; // Length of "ServiceID="
            size_t end = response.find_first_of("\r\n", start);
            if (end != std::string::npos) {
                std::string onion_address = response.substr(start, end - start) + ".onion";
                std::cout << "✅ New hidden service created: " << onion_address << std::endl;
                return onion_address;
            }
        }
        
        std::cerr << "❌ Failed to parse .onion address from response" << std::endl;
        return "";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception creating hidden service: " << e.what() << std::endl;
        return "";
    }
}