# Gotham City Seed Server

A standalone seed server for the Gotham City private network that helps nodes discover peers while maintaining maximum privacy.

## Overview

The Gotham City Seed Server is a lightweight, standalone service that:

- **Maintains active peer lists** for network bootstrapping
- **Uses GCTY protocol** with magic bytes (0x47435459) for network isolation
- **Provides peer discovery** without tracking user identities
- **Operates over Tor** for maximum privacy
- **Requires no persistent storage** of user data

## Features

### 🛡️ Privacy-First Design
- **No user tracking** - only maintains active peer lists
- **No session storage** - peers are forgotten when they disconnect
- **Tor-only operation** - all communication over .onion addresses
- **No logging** of user activities or identities

### 🌱 Peer Discovery
- **Active peer registration** - nodes can register their .onion addresses
- **Peer list distribution** - provides lists of active peers to new nodes
- **Automatic cleanup** - removes inactive peers from lists
- **Load balancing** - distributes peer lists to prevent centralization

### 🔒 Security
- **GCTY protocol validation** - only accepts properly formatted requests
- **Rate limiting** - prevents abuse and DoS attacks
- **Address validation** - ensures only valid .onion addresses are accepted
- **Capability filtering** - matches peers based on supported features

## Protocol

The seed server implements the GCTY (Gotham City) protocol:

```
Magic Bytes: 0x47435459 ("GCTY")
Protocol Version: 1
```

### Supported Message Types

1. **PEER_REGISTER** - Register a peer's .onion address
2. **PEER_DISCOVERY** - Request list of active peers
3. **PEER_UNREGISTER** - Remove peer from active list
4. **PING/PONG** - Health check and connectivity test

## Usage

### Running the Seed Server

```bash
# Build the seed server
cd gotham-seed-server
mkdir build && cd build
cmake ..
make

# Run with default settings
./gotham-seed-server

# Run with custom configuration
./gotham-seed-server --port 12345 --max-peers 1000 --cleanup-interval 300
```

### Configuration Options

- `--port` - Port to listen on (default: 12345)
- `--max-peers` - Maximum peers to track (default: 500)
- `--cleanup-interval` - Seconds between cleanup cycles (default: 180)
- `--rate-limit` - Max requests per minute per peer (default: 60)
- `--data-dir` - Directory for Tor configuration (default: ~/.gotham-seed)

### Integration with Gotham City

The main Gotham City client automatically discovers and uses seed servers:

```cpp
// In your Gotham City client
std::vector<std::string> seed_servers = {
    "your-seed1.onion",
    "your-seed2.onion", 
    "your-seed3.onion"
};

g_mesh->enableDynamicPrivacyMode(seed_servers);
```

## Deployment

### Production Deployment

1. **Deploy multiple seed servers** (recommended: 3-5)
2. **Use different hosting providers** for redundancy
3. **Monitor server health** and replace failed instances
4. **Update client configurations** with new seed addresses

### Security Considerations

- **Run on dedicated servers** with minimal attack surface
- **Use hardened Tor configurations** 
- **Monitor for abuse** and implement rate limiting
- **Regularly rotate .onion addresses** if needed

## Architecture

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Gotham Node   │    │   Seed Server   │    │   Gotham Node   │
│                 │    │                 │    │                 │
│ 1. Register     │───▶│ 2. Store peer   │    │                 │
│    with seed    │    │    in memory    │    │                 │
│                 │    │                 │    │                 │
│                 │    │ 3. Provide peer │◀───│ 4. Request      │
│                 │    │    list         │    │    peer list    │
│                 │    │                 │    │                 │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

## Building from Source

### Prerequisites

- CMake 3.28.3+
- C++20 compatible compiler
- Tor development libraries
- OpenSSL, libevent, zlib

### Build Steps

```bash
git clone <repository>
cd gotham-seed-server
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Quick Deployment

### For Server Administrators

1. **Clone and deploy:**
   ```bash
   git clone <your-repository-url>
   cd gotham-seed-server
   ./deploy.sh
   ```

2. **Get your .onion address:**
   ```bash
   sudo cat /var/lib/gotham-seed/hidden_service/hostname
   ```

3. **Share the address with Gotham City users**

### For Gotham City Users

Add the seed server to your client:

```cpp
std::vector<std::string> seed_servers = {
    "your-seed-server.onion"  // Replace with actual address
};
g_mesh->enableDynamicPrivacyMode(seed_servers);
```

## Self-Contained Design

This seed server is **completely independent** of the main Gotham City project:

- ✅ **No external dependencies** on Gotham City codebase
- ✅ **Self-contained GCTY protocol** implementation
- ✅ **Standalone build system** with CMake
- ✅ **Independent deployment** with automated scripts
- ✅ **Production-ready** with systemd integration

## Files Overview

```
gotham-seed-server/
├── README.md              # This file
├── DEPLOYMENT.md          # Detailed deployment guide
├── CMakeLists.txt         # Build configuration
├── deploy.sh              # One-command deployment script
├── include/               # Header files
│   ├── seed_server.h      # Main server class
│   ├── peer_manager.h     # Peer list management
│   ├── gcty_handler.h     # GCTY protocol handler
│   ├── tor_manager.h      # Tor service management
│   └── gcty_protocol.h    # Self-contained protocol
├── src/                   # Source files
│   ├── main.cpp           # Application entry point
│   ├── seed_server.cpp    # Main server implementation
│   ├── peer_manager.cpp   # Peer management logic
│   ├── gcty_handler.cpp   # Protocol message handling
│   ├── tor_manager.cpp    # Tor integration
│   └── gcty_protocol.cpp  # Protocol utilities
├── config/                # Configuration files
│   └── seed-server.conf.example
└── systemd/               # System service files
    └── gotham-seed-server.service.in
```

## License

This project is part of the Gotham City private network suite.