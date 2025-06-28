#include "gotham_peer_connector.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <errno.h>
#include <future>

GothamPeerConnector::GothamPeerConnector(const std::string& socks_proxy_host, int socks_proxy_port)
    : socks_host_(socks_proxy_host), socks_port_(socks_proxy_port), 
      listening_(false), running_(true), local_port_(-1), listen_socket_(-1) {
    
    std::cout << "GothamPeerConnector initialized with SOCKS proxy: " 
              << socks_host_ << ":" << socks_port_ << std::endl;
}

GothamPeerConnector::~GothamPeerConnector() {
    try {
        running_.store(false);
        
        // Stop listening with exception protection
        try {
            stopListening();
        } catch (...) {
            // Ignore exceptions in destructor
        }
        
        // Clean up all connections
        try {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            for (auto& [address, peer] : connected_peers_) {
                if (peer.socket_fd != -1) {
                    close(peer.socket_fd);
                }
            }
        } catch (...) {
            // Ignore exceptions in destructor
        }
        
        // Detach all connection threads immediately - safer for destructor
        try {
            for (auto& thread : connection_threads_) {
                if (thread.joinable()) {
                    try {
                        thread.detach();
                    } catch (...) {
                        // Ignore detach exceptions
                    }
                }
            }
        } catch (...) {
            // Ignore all exceptions in destructor
        }
    } catch (...) {
        // Catch-all to prevent any exceptions from escaping destructor
    }
}

bool GothamPeerConnector::connectToPeer(const std::string& onion_address, int port) {
    // Check if already connected
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        auto it = connected_peers_.find(onion_address);
        if (it != connected_peers_.end() && it->second.is_connected) {
            std::cout << "Already connected to peer: " << onion_address << std::endl;
            return true;
        }
    }
    
    // Create SOCKS connection
    int socket_fd = createSocksConnection(onion_address, port);
    if (socket_fd == -1) {
        std::cerr << "Failed to create SOCKS connection to " << onion_address << std::endl;
        return false;
    }
    
    // Perform Gotham handshake
    if (!performGothamHandshake(socket_fd, onion_address)) {
        std::cerr << "Failed Gotham handshake with " << onion_address << std::endl;
        close(socket_fd);
        return false;
    }
    
    // Add to connected peers
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        PeerInfo peer;
        peer.onion_address = onion_address;
        peer.port = port;
        peer.is_connected = true;
        peer.last_seen = getCurrentTimestamp();
        peer.socket_fd = socket_fd;
        peer.node_id = "unknown"; // Will be updated during handshake
        
        connected_peers_[onion_address] = peer;
    }
    
    // Start communication thread for this peer
    connection_threads_.emplace_back(&GothamPeerConnector::handlePeerCommunication, 
                                   this, onion_address, socket_fd);
    
    // Notify connection handler
    if (connection_handler_) {
        connection_handler_(onion_address, true);
    }
    
    std::cout << "Successfully connected to peer: " << onion_address << std::endl;
    return true;
}

bool GothamPeerConnector::disconnectFromPeer(const std::string& onion_address) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = connected_peers_.find(onion_address);
    if (it == connected_peers_.end()) {
        return false;
    }
    
    if (it->second.socket_fd != -1) {
        close(it->second.socket_fd);
        it->second.socket_fd = -1;
    }
    
    it->second.is_connected = false;
    
    // Notify connection handler
    if (connection_handler_) {
        connection_handler_(onion_address, false);
    }
    
    std::cout << "Disconnected from peer: " << onion_address << std::endl;
    return true;
}

std::vector<GothamPeerConnector::PeerInfo> GothamPeerConnector::getConnectedPeers() {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    std::vector<PeerInfo> peers;
    
    for (const auto& [address, peer] : connected_peers_) {
        if (peer.is_connected) {
            peers.push_back(peer);
        }
    }
    
    return peers;
}

bool GothamPeerConnector::sendMessage(const std::string& peer_address, const std::string& message) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = connected_peers_.find(peer_address);
    if (it == connected_peers_.end() || !it->second.is_connected || it->second.socket_fd == -1) {
        std::cerr << "Peer not connected: " << peer_address << std::endl;
        return false;
    }
    
    // Simple message format: [length][message]
    uint32_t msg_length = message.length();
    uint32_t net_length = htonl(msg_length);
    
    // Send length first
    ssize_t sent = send(it->second.socket_fd, &net_length, sizeof(net_length), 0);
    if (sent != sizeof(net_length)) {
        std::cerr << "Failed to send message length to " << peer_address << std::endl;
        return false;
    }
    
    // Send message
    sent = send(it->second.socket_fd, message.c_str(), msg_length, 0);
    if (sent != static_cast<ssize_t>(msg_length)) {
        std::cerr << "Failed to send message to " << peer_address << std::endl;
        return false;
    }
    
    std::cout << "Sent message to " << peer_address << ": " << message << std::endl;
    return true;
}

bool GothamPeerConnector::broadcastMessage(const std::string& message) {
    std::vector<PeerInfo> peers = getConnectedPeers();
    bool sent_to_any = false;
    
    for (const auto& peer : peers) {
        if (sendMessage(peer.onion_address, message)) {
            sent_to_any = true;
        }
    }
    
    return sent_to_any;
}

void GothamPeerConnector::setMessageHandler(MessageHandler handler) {
    message_handler_ = handler;
}

void GothamPeerConnector::setConnectionHandler(ConnectionHandler handler) {
    connection_handler_ = handler;
}

bool GothamPeerConnector::addKnownPeer(const std::string& onion_address, int port) {
    std::lock_guard<std::mutex> lock(known_peers_mutex_);
    
    std::string peer_key = onion_address + ":" + std::to_string(port);
    
    // Check if already exists
    auto it = std::find(known_peers_.begin(), known_peers_.end(), peer_key);
    if (it != known_peers_.end()) {
        return false;
    }
    
    known_peers_.push_back(peer_key);
    std::cout << "Added known peer: " << peer_key << std::endl;
    return true;
}

bool GothamPeerConnector::removeKnownPeer(const std::string& onion_address) {
    std::lock_guard<std::mutex> lock(known_peers_mutex_);
    
    auto it = std::find_if(known_peers_.begin(), known_peers_.end(),
                          [&onion_address](const std::string& peer) {
                              return peer.find(onion_address) == 0;
                          });
    
    if (it != known_peers_.end()) {
        known_peers_.erase(it);
        std::cout << "Removed known peer: " << onion_address << std::endl;
        return true;
    }
    
    return false;
}

std::vector<std::string> GothamPeerConnector::getKnownPeers() {
    std::lock_guard<std::mutex> lock(known_peers_mutex_);
    return known_peers_;
}

void GothamPeerConnector::startListening(int local_port) {
    if (listening_.load()) {
        std::cout << "Already listening on port " << local_port_ << std::endl;
        return;
    }
    
    local_port_ = local_port;
    
    // Create listening socket
    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_ == -1) {
        std::cerr << "Failed to create listening socket" << std::endl;
        return;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options" << std::endl;
        close(listen_socket_);
        return;
    }
    
    // Bind socket
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(local_port);
    
    if (bind(listen_socket_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind socket to port " << local_port << std::endl;
        close(listen_socket_);
        return;
    }
    
    // Start listening
    if (listen(listen_socket_, 5) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(listen_socket_);
        return;
    }
    
    listening_.store(true);
    listen_thread_ = std::thread(&GothamPeerConnector::listenLoop, this);
    
    std::cout << "Started listening on port " << local_port << std::endl;
}

void GothamPeerConnector::stopListening() {
    if (!listening_.load()) {
        return;
    }
    
    listening_.store(false);
    
    // Close socket to unblock any accept() calls
    if (listen_socket_ != -1) {
        close(listen_socket_);
        listen_socket_ = -1;
    }
    
    // Simple approach: give thread a moment to notice socket closure, then detach
    if (listen_thread_.joinable()) {
        try {
            // Give the thread a moment to notice the socket closure
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // Try a quick join, but if it doesn't work immediately, just detach
            try {
                // Don't wait - if thread doesn't join immediately, detach it
                listen_thread_.detach();
                std::cout << "✅ Listen thread detached for cleanup" << std::endl;
            } catch (...) {
                std::cout << "⚠️ Exception detaching listen thread" << std::endl;
            }
            
        } catch (...) {
            std::cout << "Exception stopping listen thread" << std::endl;
        }
    }
    
    std::cout << "Stopped listening" << std::endl;
}

bool GothamPeerConnector::isListening() const {
    return listening_.load();
}

// Private methods implementation

int GothamPeerConnector::createSocksConnection(const std::string& target_host, int target_port) {
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        return -1;
    }
    
    // Connect to SOCKS proxy
    struct sockaddr_in proxy_addr;
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_port = htons(socks_port_);
    inet_pton(AF_INET, socks_host_.c_str(), &proxy_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr)) < 0) {
        std::cerr << "Failed to connect to SOCKS proxy" << std::endl;
        close(sock);
        return -1;
    }
    
    // Perform SOCKS handshake
    if (!performSocksHandshake(sock, target_host, target_port)) {
        std::cerr << "SOCKS handshake failed" << std::endl;
        close(sock);
        return -1;
    }
    
    return sock;
}

bool GothamPeerConnector::performSocksHandshake(int socket, const std::string& target_host, int target_port) {
    // SOCKS5 greeting
    uint8_t greeting[] = {0x05, 0x01, 0x00}; // Version 5, 1 method, no authentication
    if (send(socket, greeting, sizeof(greeting), 0) != sizeof(greeting)) {
        return false;
    }
    
    // Read response
    uint8_t response[2];
    if (recv(socket, response, sizeof(response), 0) != sizeof(response)) {
        return false;
    }
    
    if (response[0] != 0x05 || response[1] != 0x00) {
        return false;
    }
    
    // Connection request
    std::vector<uint8_t> request;
    request.push_back(0x05); // Version
    request.push_back(0x01); // Connect command
    request.push_back(0x00); // Reserved
    request.push_back(0x03); // Domain name address type
    request.push_back(static_cast<uint8_t>(target_host.length())); // Domain length
    
    // Add domain name
    for (char c : target_host) {
        request.push_back(static_cast<uint8_t>(c));
    }
    
    // Add port
    request.push_back(static_cast<uint8_t>(target_port >> 8));
    request.push_back(static_cast<uint8_t>(target_port & 0xFF));
    
    if (send(socket, request.data(), request.size(), 0) != static_cast<ssize_t>(request.size())) {
        return false;
    }
    
    // Read connection response
    uint8_t conn_response[10];
    if (recv(socket, conn_response, sizeof(conn_response), 0) < 4) {
        return false;
    }
    
    return conn_response[0] == 0x05 && conn_response[1] == 0x00;
}

bool GothamPeerConnector::performGothamHandshake(int socket, const std::string& peer_address) {
    using namespace gotham_protocol;
    
    // Create handshake request
    HandshakeRequest request;
    request.timestamp = ProtocolUtils::getCurrentTimestamp();
    request.capabilities = static_cast<uint32_t>(NodeCapabilities::BASIC_MESSAGING) | 
                          static_cast<uint32_t>(NodeCapabilities::DHT_STORAGE);
    request.listen_port = 12345; // Default port
    ProtocolUtils::generateNodeId(request.node_id);
    
    // Create message with GCTY protocol
    std::vector<uint8_t> payload(reinterpret_cast<const uint8_t*>(&request), 
                                reinterpret_cast<const uint8_t*>(&request) + sizeof(request));
    auto message = ProtocolUtils::createMessage(MessageType::HANDSHAKE_REQUEST, payload);
    
    // Send handshake request
    if (send(socket, message.data(), message.size(), 0) != static_cast<ssize_t>(message.size())) {
        std::cerr << "Failed to send GCTY handshake request" << std::endl;
        return false;
    }
    
    // Read response header first
    MessageHeader response_header;
    ssize_t received = recv(socket, &response_header, sizeof(response_header), 0);
    if (received != sizeof(response_header)) {
        std::cerr << "Failed to receive GCTY handshake response header" << std::endl;
        return false;
    }
    
    // Convert from network byte order and validate
    ProtocolUtils::networkToHost(response_header);
    if (!ProtocolUtils::validateHeader(response_header)) {
        std::cerr << "Invalid GCTY handshake response header" << std::endl;
        return false;
    }
    
    // Check if it's a handshake response
    if (response_header.type != MessageType::HANDSHAKE_RESPONSE) {
        std::cerr << "Expected GCTY handshake response, got different message type" << std::endl;
        return false;
    }
    
    // Read response payload
    if (response_header.payload_length != sizeof(HandshakeResponse)) {
        std::cerr << "Invalid GCTY handshake response payload size" << std::endl;
        return false;
    }
    
    HandshakeResponse response;
    received = recv(socket, &response, sizeof(response), 0);
    if (received != sizeof(response)) {
        std::cerr << "Failed to receive GCTY handshake response payload" << std::endl;
        return false;
    }
    
    // Check handshake status
    if (response.status != 0) {
        std::cerr << "GCTY handshake rejected by peer" << std::endl;
        return false;
    }
    
    std::cout << "✅ GCTY handshake successful with " << peer_address.substr(0, 16) << "..." << std::endl;
    return true;
}

void GothamPeerConnector::handleIncomingConnection(int client_socket) {
    using namespace gotham_protocol;
    
    // Read incoming handshake request header
    MessageHeader request_header;
    ssize_t received = recv(client_socket, &request_header, sizeof(request_header), 0);
    if (received != sizeof(request_header)) {
        std::cerr << "Failed to receive GCTY handshake request header" << std::endl;
        close(client_socket);
        return;
    }
    
    // Convert from network byte order and validate
    ProtocolUtils::networkToHost(request_header);
    if (!ProtocolUtils::validateHeader(request_header)) {
        std::cerr << "Invalid GCTY handshake request header - rejecting connection" << std::endl;
        close(client_socket);
        return;
    }
    
    // Check if it's a handshake request
    if (request_header.type != MessageType::HANDSHAKE_REQUEST) {
        std::cerr << "Expected GCTY handshake request, got different message type - rejecting" << std::endl;
        close(client_socket);
        return;
    }
    
    // Read handshake request payload
    if (request_header.payload_length != sizeof(HandshakeRequest)) {
        std::cerr << "Invalid GCTY handshake request payload size - rejecting" << std::endl;
        close(client_socket);
        return;
    }
    
    HandshakeRequest request;
    received = recv(client_socket, &request, sizeof(request), 0);
    if (received != sizeof(request)) {
        std::cerr << "Failed to receive GCTY handshake request payload - rejecting" << std::endl;
        close(client_socket);
        return;
    }
    
    // Create handshake response
    HandshakeResponse response;
    response.timestamp = ProtocolUtils::getCurrentTimestamp();
    response.capabilities = static_cast<uint32_t>(NodeCapabilities::BASIC_MESSAGING) | 
                           static_cast<uint32_t>(NodeCapabilities::DHT_STORAGE);
    response.listen_port = 12345; // Default port
    response.status = 0; // Success
    ProtocolUtils::generateNodeId(response.node_id);
    
    // Create response message
    std::vector<uint8_t> payload(reinterpret_cast<const uint8_t*>(&response), 
                                reinterpret_cast<const uint8_t*>(&response) + sizeof(response));
    auto message = ProtocolUtils::createMessage(MessageType::HANDSHAKE_RESPONSE, payload);
    
    // Send handshake response
    if (send(client_socket, message.data(), message.size(), 0) != static_cast<ssize_t>(message.size())) {
        std::cerr << "Failed to send GCTY handshake response" << std::endl;
        close(client_socket);
        return;
    }
    
    std::cout << "✅ GCTY handshake completed with incoming peer" << std::endl;
    
    // Extract peer identifier from handshake
    std::string peer_address = "peer_" + std::string(request.node_id, 8); // Use first 8 bytes as identifier
    handlePeerCommunication(peer_address, client_socket);
}

void GothamPeerConnector::handleIncomingMessage(const std::string& from_peer, const std::string& message) {
    if (message_handler_) {
        message_handler_(from_peer, message);
    }
}

void GothamPeerConnector::listenLoop() {
    while (listening_.load() && running_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(listen_socket_, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket == -1) {
            if (listening_.load()) {
                std::cerr << "Failed to accept connection" << std::endl;
            }
            continue;
        }
        
        // Handle connection in separate thread
        connection_threads_.emplace_back(&GothamPeerConnector::handleIncomingConnection, 
                                       this, client_socket);
    }
}

void GothamPeerConnector::handlePeerCommunication(const std::string& peer_address, int socket) {
    // Set socket timeout to make recv() calls responsive to shutdown
    struct timeval timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    while (running_.load()) {
        // Read message length
        uint32_t net_length;
        ssize_t received = recv(socket, &net_length, sizeof(net_length), 0);
        if (received <= 0) {
            // Check if it's a timeout (which is expected) or a real error
            if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Timeout occurred, check running flag and continue
                continue;
            }
            // Real error or connection closed
            break;
        }
        
        uint32_t msg_length = ntohl(net_length);
        if (msg_length > 1024 * 1024) { // 1MB limit
            std::cerr << "Message too large from " << peer_address << std::endl;
            break;
        }
        
        // Read message (handle partial reads and timeouts)
        std::vector<char> buffer(msg_length);
        size_t total_received = 0;
        while (total_received < msg_length && running_.load()) {
            received = recv(socket, buffer.data() + total_received, msg_length - total_received, 0);
            if (received <= 0) {
                if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // Timeout occurred, check running flag and continue
                    continue;
                }
                // Real error or connection closed
                break;
            }
            total_received += received;
        }
        
        if (total_received != msg_length || !running_.load()) {
            break;
        }
        
        std::string message(buffer.data(), msg_length);
        
        // Update last seen timestamp
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto it = connected_peers_.find(peer_address);
            if (it != connected_peers_.end()) {
                it->second.last_seen = getCurrentTimestamp();
            }
        }
        
        // Handle message
        handleIncomingMessage(peer_address, message);
    }
    
    // Clean up connection
    close(socket);
    
    // Mark peer as disconnected
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        auto it = connected_peers_.find(peer_address);
        if (it != connected_peers_.end()) {
            it->second.is_connected = false;
            it->second.socket_fd = -1;
        }
    }
    
    // Notify connection handler
    if (connection_handler_) {
        connection_handler_(peer_address, false);
    }
}

uint64_t GothamPeerConnector::getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}