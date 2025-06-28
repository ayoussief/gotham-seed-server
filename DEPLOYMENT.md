# Gotham City Seed Server - Deployment Guide

This guide covers deploying the Gotham City Seed Server for production use.

## Quick Start

### 1. Server Requirements

**Minimum Requirements:**
- Ubuntu 20.04+ or Debian 11+ (recommended)
- 1 CPU core
- 512MB RAM
- 10GB disk space
- Stable internet connection

**Recommended for Production:**
- 2+ CPU cores
- 2GB+ RAM
- 50GB+ disk space
- SSD storage
- Dedicated server or VPS

### 2. One-Command Deployment

```bash
# Clone the repository
git clone <your-repo-url>
cd gotham-seed-server

# Run the deployment script
./deploy.sh
```

The deployment script will:
- Install all dependencies (Tor, build tools, libraries)
- Create a dedicated `gotham-seed` user
- Build and install the seed server
- Set up systemd service
- Start the server automatically

### 3. Get Your .onion Address

After deployment, get your seed server's .onion address:

```bash
sudo cat /var/lib/gotham-seed/hidden_service/hostname
```

**Save this address!** You'll need it to configure Gotham City clients.

## Manual Deployment

If you prefer manual deployment or need custom configuration:

### Step 1: Install Dependencies

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install Tor
sudo apt install -y tor

# Install build dependencies
sudo apt install -y cmake build-essential pkg-config

# Install runtime libraries
sudo apt install -y libevent-dev libssl-dev zlib1g-dev libsystemd-dev
```

### Step 2: Create User and Directories

```bash
# Create system user
sudo useradd --system --home /var/lib/gotham-seed --shell /bin/false gotham-seed

# Create directories
sudo mkdir -p /var/lib/gotham-seed
sudo mkdir -p /var/log/gotham-seed
sudo mkdir -p /etc/gotham-seed-server

# Set permissions
sudo chown gotham-seed:gotham-seed /var/lib/gotham-seed
sudo chown gotham-seed:gotham-seed /var/log/gotham-seed
sudo chmod 750 /var/lib/gotham-seed /var/log/gotham-seed
```

### Step 3: Build and Install

```bash
# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Install
sudo cp gotham-seed-server /usr/local/bin/
sudo chmod 755 /usr/local/bin/gotham-seed-server

# Install configuration
sudo cp ../config/seed-server.conf.example /etc/gotham-seed-server/seed-server.conf
sudo chown root:gotham-seed /etc/gotham-seed-server/seed-server.conf
sudo chmod 640 /etc/gotham-seed-server/seed-server.conf
```

### Step 4: Install Systemd Service

```bash
# Install service file
sudo cp gotham-seed-server.service /etc/systemd/system/
sudo systemctl daemon-reload

# Enable and start
sudo systemctl enable gotham-seed-server
sudo systemctl start gotham-seed-server
```

## Configuration

### Basic Configuration

Edit `/etc/gotham-seed-server/seed-server.conf`:

```ini
# Network settings
port=12345
max_peers=500

# Performance settings
cleanup_interval_seconds=180
rate_limit_per_minute=60

# Data directory
data_directory=/var/lib/gotham-seed

# Logging
verbose=false
```

### Advanced Configuration

For high-traffic networks:

```ini
# High-capacity settings
max_peers=2000
cleanup_interval_seconds=120
rate_limit_per_minute=120

# Enable verbose logging for monitoring
verbose=true
```

### Firewall Configuration

The seed server only needs outbound connections (Tor handles inbound):

```bash
# UFW example (optional - Tor handles networking)
sudo ufw allow out 9050  # Tor SOCKS
sudo ufw allow out 9051  # Tor control
sudo ufw allow out 80    # HTTP for Tor directory
sudo ufw allow out 443   # HTTPS for Tor directory
```

## Management

### Service Management

```bash
# Check status
sudo systemctl status gotham-seed-server

# View logs
sudo journalctl -u gotham-seed-server -f

# Restart service
sudo systemctl restart gotham-seed-server

# Stop service
sudo systemctl stop gotham-seed-server
```

### Monitoring

#### Check Server Stats

```bash
# Send SIGUSR1 to dump stats (if implemented)
sudo pkill -USR1 gotham-seed-server

# Or check logs for periodic stats
sudo journalctl -u gotham-seed-server | grep "Status:"
```

#### Monitor Resource Usage

```bash
# CPU and memory usage
top -p $(pgrep gotham-seed-server)

# Disk usage
du -sh /var/lib/gotham-seed
```

#### Monitor Network Connections

```bash
# Check Tor connections
sudo netstat -tlnp | grep tor
```

## Client Integration

### Configure Gotham City Clients

Add your seed server to client configurations:

```cpp
// In your Gotham City client code
std::vector<std::string> seed_servers = {
    "your-seed-server.onion",  // Replace with your actual .onion address
    // Add more seed servers for redundancy
};

g_mesh->enableDynamicPrivacyMode(seed_servers);
```

### Multiple Seed Servers

For production networks, deploy multiple seed servers:

```cpp
std::vector<std::string> seed_servers = {
    "seed1-abc123.onion",
    "seed2-def456.onion", 
    "seed3-ghi789.onion"
};
```

## Security

### Server Hardening

1. **Keep system updated:**
   ```bash
   sudo apt update && sudo apt upgrade -y
   ```

2. **Configure automatic security updates:**
   ```bash
   sudo apt install unattended-upgrades
   sudo dpkg-reconfigure unattended-upgrades
   ```

3. **Disable unnecessary services:**
   ```bash
   sudo systemctl disable apache2 nginx mysql # if installed
   ```

4. **Configure SSH (if using remote access):**
   ```bash
   # Disable root login, use key authentication
   sudo nano /etc/ssh/sshd_config
   ```

### Monitoring for Abuse

1. **Monitor connection patterns:**
   ```bash
   sudo journalctl -u gotham-seed-server | grep "Rate limited"
   ```

2. **Check for unusual activity:**
   ```bash
   sudo journalctl -u gotham-seed-server | grep "ERROR"
   ```

3. **Monitor resource usage:**
   ```bash
   # Set up monitoring alerts for high CPU/memory usage
   ```

### Backup and Recovery

1. **Backup configuration:**
   ```bash
   sudo cp -r /etc/gotham-seed-server /backup/
   ```

2. **Backup Tor keys (optional - will generate new .onion address):**
   ```bash
   sudo cp -r /var/lib/gotham-seed/hidden_service /backup/
   ```

## Troubleshooting

### Common Issues

#### Service Won't Start

```bash
# Check logs
sudo journalctl -u gotham-seed-server -n 50

# Check configuration
sudo -u gotham-seed /usr/local/bin/gotham-seed-server --help

# Test configuration
sudo -u gotham-seed /usr/local/bin/gotham-seed-server --data-dir /tmp/test
```

#### Tor Connection Issues

```bash
# Check Tor status
sudo systemctl status tor

# Restart Tor
sudo systemctl restart tor

# Check Tor logs
sudo journalctl -u tor -f
```

#### High Resource Usage

```bash
# Check for memory leaks
valgrind --leak-check=full /usr/local/bin/gotham-seed-server

# Monitor over time
watch -n 5 'ps aux | grep gotham-seed-server'
```

### Performance Tuning

#### For High-Traffic Networks

1. **Increase system limits:**
   ```bash
   # Add to /etc/security/limits.conf
   gotham-seed soft nofile 65536
   gotham-seed hard nofile 65536
   ```

2. **Tune kernel parameters:**
   ```bash
   # Add to /etc/sysctl.conf
   net.core.somaxconn = 1024
   net.ipv4.tcp_max_syn_backlog = 1024
   ```

3. **Increase seed server limits:**
   ```ini
   # In seed-server.conf
   max_peers=5000
   rate_limit_per_minute=200
   ```

## Production Deployment Checklist

- [ ] Server meets minimum requirements
- [ ] All dependencies installed
- [ ] Dedicated user created with proper permissions
- [ ] Service installed and enabled
- [ ] Configuration tuned for expected load
- [ ] Firewall configured (if needed)
- [ ] Monitoring set up
- [ ] Backup strategy implemented
- [ ] .onion address documented and shared
- [ ] Client configurations updated
- [ ] Testing completed with actual clients

## Support

For issues and questions:

1. Check the logs first: `sudo journalctl -u gotham-seed-server -f`
2. Review this deployment guide
3. Check the main README.md for protocol details
4. Open an issue on the project repository

## License

This deployment guide is part of the Gotham City Seed Server project.