#include "gotham_tor_mesh.h"
#include "gotham_protocol.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <random>
#include <arpa/inet.h>

GothamTorMesh::GothamTorMesh(const std::string& data_directory)
    : data_directory_(data_directory), running_(false), 
      socks_port_(9050), control_port_(9051), p2p_port_(12345),
      dynamic_privacy_enabled_(false), current_session_id_("") {
    
    // Initialize components
    tor_service_ = std::make_unique<TorService>();
    identity_manager_ = std::make_unique<TorOnionIdentityManager>(data_directory);
    
    // Note: peer_connector_ will be initialized in start() after Tor is running
    
    std::cout << "GothamTorMesh initialized with data directory: " << data_directory_ << std::endl;
}

GothamTorMesh::~GothamTorMesh() {
    stop();
}

bool GothamTorMesh::start(int socks_port, int control_port, int p2p_port) {
    if (running_) {
        std::cout << "GothamTorMesh is already running" << std::endl;
        return true;
    }
    
    socks_port_ = socks_port;
    control_port_ = control_port;
    p2p_port_ = p2p_port;
    
    std::cout << "Starting GothamTorMesh..." << std::endl;
    
    // Step 1: Start Tor service
    std::cout << "Starting Tor service..." << std::endl;
    if (!tor_service_->start(socks_port_, control_port_, data_directory_)) {
        std::cerr << "Failed to start Tor service" << std::endl;
        return false;
    }
    
    // Step 2: Wait for Tor to be ready
    std::cout << "Waiting for Tor to be ready..." << std::endl;
    if (!waitForTorReady(30)) {
        std::cerr << "Tor failed to become ready within timeout" << std::endl;
        tor_service_->stop();
        return false;
    }
    
    // Step 3: Generate fresh dynamic identity for this session
    std::cout << "🎭 Generating fresh .onion identity for maximum privacy..." << std::endl;
    
    // Generate unique session ID
    current_session_id_ = generateSessionId();
    std::string service_name = "gotham_session_" + current_session_id_;
    
    if (!identity_manager_->createIdentity(service_name, p2p_port_, p2p_port_)) {
        std::cerr << "Failed to create dynamic session identity" << std::endl;
        tor_service_->stop();
        return false;
    }
    
    std::cout << "✅ Fresh .onion identity generated!" << std::endl;
    std::cout << "🔒 This address is unique to this session and will never be reused" << std::endl;
    
    // Step 4: Initialize peer connector
    peer_connector_ = std::make_unique<GothamPeerConnector>("127.0.0.1", socks_port_);
    
    // Set up internal handlers
    peer_connector_->setMessageHandler(
        [this](const std::string& from, const std::string& msg) {
            internalMessageHandler(from, msg);
        }
    );
    
    peer_connector_->setConnectionHandler(
        [this](const std::string& peer, bool connected) {
            internalConnectionHandler(peer, connected);
        }
    );
    
    // Step 5: Start listening for incoming connections
    peer_connector_->startListening(p2p_port_);
    
    // Step 6: Initialize bootstrap peers and start discovery
    initializeDefaultPeers();
    startPeerDiscovery();
    
    running_ = true;
    
    std::cout << "GothamTorMesh started successfully!" << std::endl;
    std::cout << "🧅 Session .onion address: " << getMyOnionAddress() << std::endl;
    std::cout << "🔌 Listening on port: " << p2p_port_ << std::endl;
    std::cout << "🎭 Dynamic Privacy Mode: Fresh identity generated for this session" << std::endl;
    
    // If dynamic privacy mode is enabled, bootstrap from seeds
    if (dynamic_privacy_enabled_) {
        std::cout << "🌱 Bootstrapping from seed servers..." << std::endl;
        int discovered_peers = bootstrapFromSeeds();
        std::cout << "🔍 Discovered " << discovered_peers << " peers from seeds" << std::endl;
        
        // Register with seeds so other nodes can find us
        if (registerWithSeeds()) {
            std::cout << "📡 Successfully registered with seed servers" << std::endl;
        } else {
            std::cout << "⚠️ Failed to register with seed servers (will retry in background)" << std::endl;
        }
    }
    
    return true;
}

void GothamTorMesh::stop() {
    if (!running_) {
        return;
    }
    
    std::cout << "Stopping GothamTorMesh..." << std::endl;
    
    running_ = false;
    
    // Stop peer connector first with timeout protection
    if (peer_connector_) {
        std::cout << "🔌 Stopping peer connector..." << std::endl;
        
        // Use timeout to prevent hanging on peer connector shutdown
        std::atomic<bool> peer_stopped{false};
        std::thread peer_shutdown([&]() {
            try {
                peer_connector_->stopListening();
                peer_stopped.store(true);
            } catch (...) {
                peer_stopped.store(true); // Mark as done even if exception
            }
        });
        
        // Wait up to 2 seconds for peer connector to stop
        auto start_time = std::chrono::steady_clock::now();
        while (!peer_stopped.load() && 
               std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - start_time).count() < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (peer_shutdown.joinable()) {
            if (peer_stopped.load()) {
                peer_shutdown.join();
                std::cout << "✅ Peer connector stopped cleanly" << std::endl;
            } else {
                peer_shutdown.detach(); // Let it finish in background
                std::cout << "⚠️ Peer connector shutdown timeout - continuing..." << std::endl;
            }
        }
        
        // Reset peer connector safely
        try {
            peer_connector_.reset();
        } catch (...) {
            std::cout << "⚠️ Exception during peer connector cleanup - continuing..." << std::endl;
        }
    }
    
    // Stop Tor service with timeout protection
    if (tor_service_) {
        std::cout << "🧅 Stopping Tor service..." << std::endl;
        
        // Use a separate thread with timeout to prevent hanging
        std::atomic<bool> tor_stopped{false};
        std::thread tor_shutdown([&]() {
            try {
                tor_service_->stop();
                tor_stopped.store(true);
            } catch (...) {
                tor_stopped.store(true); // Mark as done even if exception
            }
        });
        
        // Wait up to 5 seconds for Tor to stop
        auto start_time = std::chrono::steady_clock::now();
        while (!tor_stopped.load() && 
               std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - start_time).count() < 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (tor_shutdown.joinable()) {
            if (tor_stopped.load()) {
                tor_shutdown.join();
                std::cout << "✅ Tor service stopped cleanly" << std::endl;
            } else {
                tor_shutdown.detach(); // Let it finish in background
                std::cout << "⚠️ Tor service shutdown timeout - continuing..." << std::endl;
                
                // Force kill any remaining tor processes
                std::cout << "🔪 Force killing remaining Tor processes..." << std::endl;
                system("pkill -9 -f 'tor.*DataDirectory' 2>/dev/null || true");
            }
        }
    }
    
    std::cout << "✅ GothamTorMesh stopped" << std::endl;
}

bool GothamTorMesh::isRunning() const {
    return running_;
}

std::string GothamTorMesh::getMyOnionAddress() {
    if (!tor_service_ || !tor_service_->isRunning()) {
        return "";
    }
    
    // Always return the current session's .onion address
    return tor_service_->getOnionAddress();
}

bool GothamTorMesh::addTrustedPeer(const std::string& onion_address) {
    if (!TorOnionIdentityManager::isValidOnionAddress(onion_address)) {
        std::cerr << "Invalid onion address: " << onion_address << std::endl;
        return false;
    }
    
    if (peer_connector_) {
        return peer_connector_->addKnownPeer(onion_address, p2p_port_);
    }
    
    // If peer connector not initialized yet, add to default peers
    default_bootstrap_peers_.push_back(onion_address);
    return true;
}

std::vector<std::string> GothamTorMesh::getTrustedPeers() {
    if (peer_connector_) {
        return peer_connector_->getKnownPeers();
    }
    
    return default_bootstrap_peers_;
}

bool GothamTorMesh::removeTrustedPeer(const std::string& onion_address) {
    if (peer_connector_) {
        return peer_connector_->removeKnownPeer(onion_address);
    }
    
    // Remove from default peers
    auto it = std::find(default_bootstrap_peers_.begin(), default_bootstrap_peers_.end(), onion_address);
    if (it != default_bootstrap_peers_.end()) {
        default_bootstrap_peers_.erase(it);
        return true;
    }
    
    return false;
}

bool GothamTorMesh::sendMessage(const std::string& peer_address, const std::string& message) {
    if (!peer_connector_ || !running_) {
        std::cerr << "Mesh not running or peer connector not initialized" << std::endl;
        return false;
    }
    
    return peer_connector_->sendMessage(peer_address, message);
}

bool GothamTorMesh::broadcastMessage(const std::string& message) {
    if (!peer_connector_ || !running_) {
        std::cerr << "Mesh not running or peer connector not initialized" << std::endl;
        return false;
    }
    
    return peer_connector_->broadcastMessage(message);
}

void GothamTorMesh::setMessageHandler(std::function<void(const std::string&, const std::string&)> handler) {
    user_message_handler_ = handler;
}

void GothamTorMesh::setPeerConnectionHandler(std::function<void(const std::string&, bool)> handler) {
    user_connection_handler_ = handler;
}

int GothamTorMesh::getConnectedPeerCount() {
    if (!peer_connector_) {
        return 0;
    }
    
    return peer_connector_->getConnectedPeers().size();
}

std::vector<std::string> GothamTorMesh::getConnectedPeers() {
    std::vector<std::string> peer_addresses;
    
    if (peer_connector_) {
        auto peers = peer_connector_->getConnectedPeers();
        for (const auto& peer : peers) {
            peer_addresses.push_back(peer.onion_address);
        }
    }
    
    return peer_addresses;
}

std::vector<GothamPeerConnector::PeerInfo> GothamTorMesh::getConnectedPeersInfo() {
    if (!peer_connector_) {
        return {};
    }
    
    return peer_connector_->getConnectedPeers();
}

GothamPeerConnector* GothamTorMesh::getPeerConnector() {
    return peer_connector_.get();
}

int GothamTorMesh::connectToAllTrustedPeers() {
    if (!peer_connector_ || !running_) {
        return 0;
    }
    
    auto trusted_peers = getTrustedPeers();
    int successful_connections = 0;
    
    for (const auto& peer_key : trusted_peers) {
        // Parse peer_key format: "onion_address:port"
        size_t colon_pos = peer_key.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }
        
        std::string onion_address = peer_key.substr(0, colon_pos);
        int port = std::stoi(peer_key.substr(colon_pos + 1));
        
        if (peer_connector_->connectToPeer(onion_address, port)) {
            successful_connections++;
        }
        
        // Small delay between connection attempts
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::cout << "Connected to " << successful_connections << " out of " 
              << trusted_peers.size() << " trusted peers" << std::endl;
    
    return successful_connections;
}

bool GothamTorMesh::exportMyIdentity(const std::string& export_path) {
    if (!identity_manager_) {
        return false;
    }
    
    return identity_manager_->exportIdentity("gotham_main", export_path);
}

bool GothamTorMesh::importPeerIdentity(const std::string& import_path, const std::string& service_name) {
    if (!identity_manager_) {
        return false;
    }
    
    return identity_manager_->importIdentity(service_name, import_path);
}

std::string GothamTorMesh::getNetworkStats() {
    std::ostringstream stats;
    
    stats << "=== Gotham Tor Mesh Network Statistics ===" << std::endl;
    stats << "Status: " << (running_ ? "Running" : "Stopped") << std::endl;
    stats << "My Onion Address: " << getMyOnionAddress() << std::endl;
    stats << "SOCKS Port: " << socks_port_ << std::endl;
    stats << "Control Port: " << control_port_ << std::endl;
    stats << "P2P Port: " << p2p_port_ << std::endl;
    stats << "Connected Peers: " << getConnectedPeerCount() << std::endl;
    stats << "Trusted Peers: " << getTrustedPeers().size() << std::endl;
    
    if (tor_service_) {
        stats << "Tor Version: " << TorService::getVersion() << std::endl;
        stats << "Tor Running: " << (tor_service_->isRunning() ? "Yes" : "No") << std::endl;
    }
    
    stats << std::endl << "Connected Peers Details:" << std::endl;
    auto peers_info = getConnectedPeersInfo();
    for (const auto& peer : peers_info) {
        stats << "  - " << peer.onion_address << ":" << peer.port 
              << " (Node ID: " << peer.node_id << ")" << std::endl;
    }
    
    return stats.str();
}

// Private methods

void GothamTorMesh::initializeDefaultPeers() {
    // Add any default bootstrap peers here
    // For now, we'll rely on manually added trusted peers
    
    std::cout << "Initialized with " << default_bootstrap_peers_.size() << " default peers" << std::endl;
}

void GothamTorMesh::startPeerDiscovery() {
    if (!peer_connector_) {
        return;
    }
    
    // Add default peers to peer connector
    for (const auto& peer_address : default_bootstrap_peers_) {
        peer_connector_->addKnownPeer(peer_address, p2p_port_);
    }
    
    // Start connecting to trusted peers after a short delay
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        connectToAllTrustedPeers();
    }).detach();
}

bool GothamTorMesh::waitForTorReady(int timeout_seconds) {
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(timeout_seconds);
    
    while (std::chrono::steady_clock::now() - start_time < timeout) {
        if (tor_service_->isRunning()) {
            // Check if onion address is available
            std::string onion_addr = tor_service_->getOnionAddress();
            if (!onion_addr.empty()) {
                std::cout << "Tor is ready! Onion address: " << onion_addr << std::endl;
                return true;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    return false;
}

void GothamTorMesh::internalMessageHandler(const std::string& from_peer, const std::string& message) {
    std::cout << "Received message from " << from_peer << ": " << message << std::endl;
    
    // Call user handler if set
    if (user_message_handler_) {
        user_message_handler_(from_peer, message);
    }
}

void GothamTorMesh::internalConnectionHandler(const std::string& peer_address, bool connected) {
    std::cout << "Peer " << peer_address << (connected ? " connected" : " disconnected") << std::endl;
    
    // Call user handler if set
    if (user_connection_handler_) {
        user_connection_handler_(peer_address, connected);
    }
}

// Dynamic Privacy Mode Implementation

bool GothamTorMesh::enableDynamicPrivacyMode(const std::vector<std::string>& seed_servers) {
    if (running_) {
        std::cerr << "❌ Cannot enable dynamic privacy mode while mesh is running" << std::endl;
        return false;
    }
    
    if (seed_servers.empty()) {
        std::cerr << "❌ At least one seed server is required for dynamic privacy mode" << std::endl;
        return false;
    }
    
    // Validate seed server addresses
    for (const auto& seed : seed_servers) {
        if (!TorOnionIdentityManager::isValidOnionAddress(seed)) {
            std::cerr << "❌ Invalid seed server address: " << seed << std::endl;
            return false;
        }
    }
    
    dynamic_privacy_enabled_ = true;
    seed_servers_ = seed_servers;
    
    std::cout << "🎭 Dynamic Privacy Mode enabled!" << std::endl;
    std::cout << "   🔄 Fresh .onion address generated every session" << std::endl;
    std::cout << "   🌱 Bootstrap through " << seed_servers.size() << " seed servers" << std::endl;
    std::cout << "   🛡️ Maximum privacy protection - no persistent identities" << std::endl;
    
    return true;
}

bool GothamTorMesh::isDynamicPrivacyEnabled() const {
    return dynamic_privacy_enabled_;
}

int GothamTorMesh::bootstrapFromSeeds() {
    if (!dynamic_privacy_enabled_ || seed_servers_.empty()) {
        return 0;
    }
    
    int discovered_peers = 0;
    
    for (const auto& seed_address : seed_servers_) {
        std::cout << "🌱 Contacting seed server: " << seed_address.substr(0, 16) << "..." << std::endl;
        
        // Connect to seed server using GCTY protocol
        if (peer_connector_) {
            if (peer_connector_->connectToPeer(seed_address, 12345)) {
                // Create GCTY peer discovery request
                using namespace gotham_protocol;
                
                struct PeerDiscoveryRequest {
                    uint16_t max_peers;
                    uint32_t required_capabilities;
                    uint32_t reserved;
                } __attribute__((packed));
                
                PeerDiscoveryRequest request;
                request.max_peers = htons(20);  // Request up to 20 peers
                request.required_capabilities = htonl(
                    static_cast<uint32_t>(NodeCapabilities::BASIC_MESSAGING) |
                    static_cast<uint32_t>(NodeCapabilities::DHT_STORAGE)
                );
                request.reserved = 0;
                
                // Create GCTY message
                std::vector<uint8_t> payload(reinterpret_cast<const uint8_t*>(&request),
                                           reinterpret_cast<const uint8_t*>(&request) + sizeof(request));
                auto message = ProtocolUtils::createMessage(MessageType::PEER_DISCOVERY, payload);
                
                // Send raw GCTY message via peer connector
                std::string raw_message(message.begin(), message.end());
                if (peer_connector_->sendMessage(seed_address, raw_message)) {
                    std::cout << "✅ Successfully contacted seed: " << seed_address.substr(0, 16) << "..." << std::endl;
                    discovered_peers++; // Placeholder - would parse actual peer list
                } else {
                    std::cout << "⚠️ Failed to send discovery request to seed: " << seed_address.substr(0, 16) << "..." << std::endl;
                }
            } else {
                std::cout << "⚠️ Failed to connect to seed: " << seed_address.substr(0, 16) << "..." << std::endl;
            }
        }
    }
    
    return discovered_peers;
}

bool GothamTorMesh::registerWithSeeds() {
    if (!dynamic_privacy_enabled_ || seed_servers_.empty()) {
        return false;
    }
    
    std::string my_address = getMyOnionAddress();
    if (my_address.empty()) {
        return false;
    }
    
    bool registered_with_any = false;
    
    for (const auto& seed_address : seed_servers_) {
        std::cout << "📡 Registering with seed server: " << seed_address.substr(0, 16) << "..." << std::endl;
        
        if (peer_connector_) {
            if (peer_connector_->connectToPeer(seed_address, 12345)) {
                // Create GCTY peer registration request
                using namespace gotham_protocol;
                
                struct PeerRegisterRequest {
                    uint16_t port;
                    uint32_t capabilities;
                    char onion_address[64];
                } __attribute__((packed));
                
                PeerRegisterRequest request;
                request.port = htons(p2p_port_);
                request.capabilities = htonl(
                    static_cast<uint32_t>(NodeCapabilities::BASIC_MESSAGING) |
                    static_cast<uint32_t>(NodeCapabilities::DHT_STORAGE)
                );
                strncpy(request.onion_address, my_address.c_str(), sizeof(request.onion_address) - 1);
                request.onion_address[sizeof(request.onion_address) - 1] = '\0';
                
                // Create GCTY message
                std::vector<uint8_t> payload(reinterpret_cast<const uint8_t*>(&request),
                                           reinterpret_cast<const uint8_t*>(&request) + sizeof(request));
                auto message = ProtocolUtils::createMessage(MessageType::PEER_REGISTER, payload);
                
                // Send raw GCTY message via peer connector
                std::string raw_message(message.begin(), message.end());
                if (peer_connector_->sendMessage(seed_address, raw_message)) {
                    std::cout << "✅ Successfully registered with seed: " << seed_address.substr(0, 16) << "..." << std::endl;
                    registered_with_any = true;
                } else {
                    std::cout << "⚠️ Failed to send registration to seed: " << seed_address.substr(0, 16) << "..." << std::endl;
                }
            } else {
                std::cout << "⚠️ Failed to connect to seed for registration: " << seed_address.substr(0, 16) << "..." << std::endl;
            }
        }
    }
    
    return registered_with_any;
}

std::vector<std::string> GothamTorMesh::getSeedServers() const {
    return seed_servers_;
}

std::string GothamTorMesh::generateSessionId() {
    // Generate a unique session ID using timestamp + random component
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    // Add random component for uniqueness
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    uint32_t random_part = dis(gen);
    
    std::stringstream ss;
    ss << std::hex << timestamp << "_" << random_part;
    return ss.str();
}

// for later .. privecy 
/* Exactly — you've got the right idea. Let's break it down clearly:

---

Sign all seed lists and verify them in clients

✅ Best Practice Recap

    Use 2–5 .onion seeders

    Shuffle and rotate regularly

    Require signature validation of seed lists

    Score + validate peers post-bootstrap

    Rely on DHT + gossip once inside the network

## ✅ Gotham Tor DNS Seeders — Minimal, Private, Scalable

### 🔹 You Only Need:

* 2 or 3 **trusted `.onion` seeders** baked into the client.

  * Example: `seed1.gotham.onion`, `seed2.gotham.onion`, `seed3.gotham.onion`
* These act as **anonymous bootstrap providers**, *not full nodes* or coordinators.

---

## 🔒 What They Do (and **Don’t** Do)

### ✔️ They DO:

* Serve a **list of peer onion addresses** (e.g. JSON, raw text, protobuf).
* Optionally **sign the list** (Ed25519/NaCl signature).
* Accept POSTs with peer suggestions (optional, for "gossip sync").

### ❌ They DON’T:

* Track users (Tor hides IP/identity).
* Maintain sessions.
* Interact with your traffic or identity.
* Know what you're doing in the network.
* Store DHT data — they're not part of the mesh.

---

## 📦 Example Flow

### 🛠️ Gotham client startup:

```cpp
// Try each DNS seeder until one responds
for (const auto& seeder : hardcoded_onion_seeders) {
    auto peer_list = fetchPeerListFromSeeder(seeder);  // via SOCKS5
    if (peer_list.valid()) {
        std::shuffle(peer_list.begin(), peer_list.end(), rng);
        for (const auto& peer : peer_list) {
            dht->addPeer(peer);
        }
        break;
    }
}
```

### 🎁 Seeder response (`/peers` or `/bootstrap.json`)

```json
{
  "generated_at": 1721930934,
  "peers": [
    "qwerty123456.onion:12345",
    "zxcvb789012.onion:12345",
    "abcd1111abcd.onion:12345"
  ],
  "signature": "base64-ed25519-signature"
}
```

### ✅ Client verifies:

* Signature
* Valid `.onion` formats
* Deduplicates trusted peers
* Injects into DHT/gossip

---

## 🌐 Bonus: Trusted-Score Based DHT Bootstrapping

Once you're inside the network:

* Peers advertise a `trust_score` (reputation).
* Your DHT `routing_table` prefers peers:

  * With high uptime
  * Whitelisted via DNS seed
  * Manually marked as trusted

You can add a score system like:

```cpp
struct PeerEntry {
    std::string onion_address;
    int port;
    int trust_score; // 0-100
    bool signed_by_seed = false;
};
```

Then use it to sort peer entries in DHT’s routing buckets or for prioritizing `FIND_NODE`.

---

## 📎 Summary

| Feature                          | Value                        |
| -------------------------------- | ---------------------------- |
| **Only 3 `.onion` seeds known**  | ✅ Yes                        |
| **They serve peer lists only**   | ✅ Yes                        |
| **They never track or store**    | ✅ Tor ensures privacy        |
| **Rotatable addresses**          | ✅ Just update next version   |
| **Signed peer lists**            | ✅ Strong trust chain         |
| **Used only on startup**         | ✅ Then DHT/gossip takes over |
| **Gotham remains decentralized** | ✅ Always                     |

---

Let me know if you want:

* 🔐 `seed_list_signer.py` to generate signed peer lists
* 🌐 Tiny Flask `.onion` seed server template
* ⚙️ `fetch_and_verify_peers()` C++ example

Your Gotham network is shaping up to be more private than even Bitcoin’s P2P stack.
*/