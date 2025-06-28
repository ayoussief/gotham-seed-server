#include "seed_server.h"
#include "peer_manager.h"
#include "gcty_handler.h"
#include "tor_manager.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <unistd.h>
#include <sys/socket.h>

SeedServer::SeedServer(const Config& config)
    : config_(config), running_(false), shutdown_requested_(false) {
    
    std::cout << "🏗️ Initializing Gotham City Seed Server..." << std::endl;
}

SeedServer::~SeedServer() {
    stop();
}

bool SeedServer::start() {
    if (running_) {
        return true;
    }
    
    if (!initialize()) {
        cleanup();
        return false;
    }
    
    // Start background threads
    server_thread_ = std::thread(&SeedServer::serverLoop, this);
    cleanup_thread_ = std::thread(&SeedServer::cleanupLoop, this);
    
    running_ = true;
    return true;
}

void SeedServer::stop() {
    if (!running_) {
        return;
    }
    
    log("INFO", "Initiating graceful shutdown...");
    
    shutdown_requested_ = true;
    running_ = false;
    
    // Wait for threads to finish
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    cleanup();
    
    log("INFO", "Shutdown complete");
}

bool SeedServer::isRunning() const {
    return running_;
}

std::string SeedServer::getStats() const {
    if (!peer_manager_ || !gcty_handler_) {
        return "Server not initialized";
    }
    
    auto peer_stats = peer_manager_->getStats();
    auto handler_stats = gcty_handler_->getStats();
    
    // Calculate uptime
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - peer_stats.server_start_time);
    
    std::ostringstream oss;
    oss << "=== Gotham City Seed Server Statistics ===\n";
    oss << "Uptime: " << uptime.count() << " seconds\n";
    oss << "Configuration:\n";
    oss << "  Port: " << config_.port << "\n";
    oss << "  Max Peers: " << config_.max_peers << "\n";
    oss << "  Rate Limit: " << config_.rate_limit_per_minute << " req/min\n";
    oss << "  Cleanup Interval: " << config_.cleanup_interval_seconds << "s\n";
    oss << "\nPeer Statistics:\n";
    oss << "  Total Peers: " << peer_stats.total_peers << "\n";
    oss << "  Active Peers: " << peer_stats.active_peers << "\n";
    oss << "  Registrations Processed: " << peer_stats.registrations_processed << "\n";
    oss << "  Discovery Requests Served: " << peer_stats.requests_served << "\n";
    oss << "\n" << handler_stats << "\n";
    
    if (tor_manager_) {
        oss << "\nNetwork:\n";
        oss << "  Onion Address: " << tor_manager_->getOnionAddress() << "\n";
        oss << "  Tor Status: " << (tor_manager_->isRunning() ? "Running" : "Stopped") << "\n";
    }
    
    return oss.str();
}

std::string SeedServer::getOnionAddress() const {
    if (tor_manager_) {
        return tor_manager_->getOnionAddress();
    }
    return "";
}

void SeedServer::serverLoop() {
    log("INFO", "Server loop started");
    
    while (!shutdown_requested_) {
        // Main server loop - most work is done in event handlers
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Periodic status logging
        static auto last_status = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        
        if (config_.verbose && 
            std::chrono::duration_cast<std::chrono::minutes>(now - last_status).count() >= 5) {
            
            if (peer_manager_) {
                auto stats = peer_manager_->getStats();
                log("INFO", "Status: " + std::to_string(stats.active_peers) + " active peers, " +
                           std::to_string(stats.requests_served) + " requests served");
            }
            last_status = now;
        }
    }
    
    log("INFO", "Server loop ended");
}

void SeedServer::cleanupLoop() {
    log("INFO", "Cleanup loop started");
    
    while (!shutdown_requested_) {
        // Wait for cleanup interval
        for (int i = 0; i < config_.cleanup_interval_seconds && !shutdown_requested_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (shutdown_requested_) {
            break;
        }
        
        // Perform cleanup
        if (peer_manager_) {
            size_t removed = peer_manager_->cleanupInactivePeers(300); // 5 minutes
            if (removed > 0) {
                log("INFO", "Cleaned up " + std::to_string(removed) + " inactive peers");
            }
        }
    }
    
    log("INFO", "Cleanup loop ended");
}

bool SeedServer::initialize() {
    log("INFO", "Initializing components...");
    
    // Initialize peer manager
    peer_manager_ = std::make_unique<PeerManager>(config_.max_peers, config_.rate_limit_per_minute);
    
    // Initialize GCTY handler (convert unique_ptr to shared_ptr)
    std::shared_ptr<PeerManager> shared_peer_manager(peer_manager_.get(), [](PeerManager*){});
    gcty_handler_ = std::make_unique<GCTYHandler>(shared_peer_manager);
    
    // Initialize Tor manager
    tor_manager_ = std::make_unique<TorManager>(config_.data_directory, config_.port);
    
    // Set up connection handler
    tor_manager_->setConnectionHandler([this](int socket_fd, const std::string& peer_address) {
        handleConnection(socket_fd, peer_address);
    });
    
    // Start Tor
    if (!tor_manager_->start()) {
        log("ERROR", "Failed to start Tor manager");
        return false;
    }
    
    // Start listening for connections
    if (!tor_manager_->startListening()) {
        log("ERROR", "Failed to start listening for connections");
        return false;
    }
    
    log("INFO", "All components initialized successfully");
    return true;
}

void SeedServer::cleanup() {
    log("INFO", "Cleaning up components...");
    
    if (tor_manager_) {
        tor_manager_->stop();
        tor_manager_.reset();
    }
    
    gcty_handler_.reset();
    peer_manager_.reset();
    
    log("INFO", "Cleanup complete");
}

void SeedServer::log(const std::string& level, const std::string& message) const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::cout << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] "
              << "[" << level << "] " << message << std::endl;
}

void SeedServer::handleConnection(int socket_fd, const std::string& peer_address) {
    if (config_.verbose) {
        log("DEBUG", "New connection from " + peer_address);
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 30;  // 30 second timeout
    timeout.tv_usec = 0;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    try {
        // Read incoming message
        std::vector<uint8_t> buffer(4096);  // 4KB buffer
        ssize_t received = recv(socket_fd, buffer.data(), buffer.size(), 0);
        
        if (received <= 0) {
            if (config_.verbose) {
                log("DEBUG", "No data received from " + peer_address);
            }
            close(socket_fd);
            return;
        }
        
        buffer.resize(received);
        
        // Process message with GCTY handler
        bool handled = gcty_handler_->processMessage(buffer, peer_address, 
            [socket_fd](const std::vector<uint8_t>& response) {
                // Send response
                send(socket_fd, response.data(), response.size(), 0);
            });
        
        if (config_.verbose) {
            log("DEBUG", "Message from " + peer_address + " " + (handled ? "handled" : "rejected"));
        }
        
    } catch (const std::exception& e) {
        log("ERROR", "Exception handling connection from " + peer_address + ": " + e.what());
    }
    
    // Close connection
    close(socket_fd);
}