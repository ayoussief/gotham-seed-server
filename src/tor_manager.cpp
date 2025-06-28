#include "tor_manager.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

TorManager::TorManager(const std::string& data_directory, int port)
    : data_directory_(data_directory), port_(port), listening_(false), listen_socket_(-1) {
    
    tor_service_ = std::make_unique<TorService>();
    
    std::cout << "🔧 TorManager initialized with tor-wrapper (data_dir: " << data_directory 
              << ", port: " << port << ")" << std::endl;
}

TorManager::~TorManager() {
    stop();
}

bool TorManager::start() {
    // Use tor-wrapper with dynamic port allocation to avoid conflicts
    int socks_port = 9150;  // Different from default 9050
    int control_port = 9151; // Different from default 9051
    
    if (!tor_service_->start(socks_port, control_port, data_directory_)) {
        std::cerr << "❌ Failed to start Tor service" << std::endl;
        return false;
    }
    
    // Wait for hidden service to be ready
    std::cout << "⏳ Waiting for hidden service to initialize..." << std::endl;
    std::string onion_address;
    for (int i = 0; i < 30; ++i) {
        onion_address = getOnionAddress();
        if (!onion_address.empty()) {
            std::cout << "✅ Hidden service initialized successfully!" << std::endl;
            std::cout << "🧅 Hidden service address: " << onion_address << std::endl;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    std::cerr << "❌ Timeout waiting for hidden service to initialize" << std::endl;
    return false;
}

void TorManager::stop() {
    stopListening();
    if (tor_service_) {
        tor_service_->stop();
    }
}

bool TorManager::isRunning() const {
    return tor_service_ && tor_service_->isRunning();
}

std::string TorManager::getOnionAddress() const {
    if (!isRunning()) {
        return "";
    }
    
    return tor_service_->getOnionAddress();
}

void TorManager::setConnectionHandler(ConnectionHandler handler) {
    connection_handler_ = handler;
}

bool TorManager::startListening() {
    if (listening_ || !isRunning()) {
        return false;
    }
    
    if (!createListenSocket()) {
        return false;
    }
    
    listening_ = true;
    listen_thread_ = std::thread(&TorManager::listenLoop, this);
    
    std::cout << "🔌 Started listening for connections on port " << port_ << std::endl;
    return true;
}

void TorManager::stopListening() {
    if (!listening_) {
        return;
    }
    
    listening_ = false;
    
    if (listen_socket_ != -1) {
        close(listen_socket_);
        listen_socket_ = -1;
    }
    
    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }
    
    std::cout << "🔌 Stopped listening for connections" << std::endl;
}

std::string TorManager::getVersion() {
    return "TorWrapper-1.0";
}

bool TorManager::createListenSocket() {
    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_ == -1) {
        std::cerr << "❌ Failed to create listen socket" << std::endl;
        return false;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        std::cerr << "⚠️ Failed to set SO_REUSEADDR" << std::endl;
    }
    
    // Bind to localhost
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    
    if (bind(listen_socket_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "❌ Failed to bind to port " << port_ << ": " << strerror(errno) << std::endl;
        close(listen_socket_);
        listen_socket_ = -1;
        return false;
    }
    
    // Start listening
    if (listen(listen_socket_, 128) == -1) {
        std::cerr << "❌ Failed to listen on socket" << std::endl;
        close(listen_socket_);
        listen_socket_ = -1;
        return false;
    }
    
    return true;
}

void TorManager::listenLoop() {
    while (listening_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(listen_socket_, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket == -1) {
            if (listening_) {
                std::cerr << "⚠️ Failed to accept connection: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        // Handle connection in separate thread
        std::thread(&TorManager::handleConnection, this, client_socket).detach();
    }
}

void TorManager::handleConnection(int client_socket) {
    std::string peer_address = getPeerAddress(client_socket);
    
    if (connection_handler_) {
        connection_handler_(client_socket, peer_address);
    } else {
        // No handler set, just close the connection
        close(client_socket);
    }
}

std::string TorManager::getPeerAddress(int socket_fd) {
    // For connections through Tor, we can't get the real peer address
    // Generate a unique identifier based on the socket
    std::ostringstream oss;
    oss << "peer_" << socket_fd << "_" << std::chrono::steady_clock::now().time_since_epoch().count();
    return oss.str();
}