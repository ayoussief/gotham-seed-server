#!/bin/bash

# Gotham City Seed Server Deployment Script
# This script sets up the seed server for production deployment

set -e

echo "🦇 Gotham City Seed Server Deployment Script"
echo "=============================================="

# Check if running as root
if [[ $EUID -eq 0 ]]; then
   echo "❌ This script should not be run as root for security reasons"
   echo "   Please run as a regular user with sudo privileges"
   exit 1
fi

# Check for required dependencies
echo "🔍 Checking dependencies..."

# Check for Tor
if ! command -v tor &> /dev/null; then
    echo "❌ Tor is not installed. Installing..."
    sudo apt-get update
    sudo apt-get install -y tor
fi

# Check for build dependencies
if ! command -v cmake &> /dev/null; then
    echo "❌ CMake is not installed. Installing build dependencies..."
    sudo apt-get install -y cmake build-essential pkg-config
fi

# Check for required libraries
echo "📦 Installing required libraries..."
sudo apt-get install -y \
    libevent-dev \
    libssl-dev \
    zlib1g-dev \
    libsystemd-dev

# Create seed server user (if not exists)
if ! id "gotham-seed" &>/dev/null; then
    echo "👤 Creating gotham-seed user..."
    sudo useradd --system --home /var/lib/gotham-seed --shell /bin/false gotham-seed
fi

# Create directories
echo "📁 Creating directories..."
sudo mkdir -p /var/lib/gotham-seed
sudo mkdir -p /var/log/gotham-seed
sudo mkdir -p /etc/gotham-seed-server

# Set permissions
sudo chown gotham-seed:gotham-seed /var/lib/gotham-seed
sudo chown gotham-seed:gotham-seed /var/log/gotham-seed
sudo chmod 750 /var/lib/gotham-seed
sudo chmod 750 /var/log/gotham-seed

# Build the seed server
echo "🔨 Building seed server..."
mkdir -p build
cd build
cmake ..
make -j$(nproc)

# Install the binary
echo "📦 Installing seed server..."
sudo cp gotham-seed-server /usr/local/bin/
sudo chmod 755 /usr/local/bin/gotham-seed-server

# Install configuration
if [ ! -f /etc/gotham-seed-server/seed-server.conf ]; then
    echo "⚙️ Installing default configuration..."
    sudo cp ../config/seed-server.conf.example /etc/gotham-seed-server/seed-server.conf
    sudo chown root:gotham-seed /etc/gotham-seed-server/seed-server.conf
    sudo chmod 640 /etc/gotham-seed-server/seed-server.conf
fi

# Install systemd service
echo "🔧 Installing systemd service..."
sudo cp gotham-seed-server.service /etc/systemd/system/
sudo systemctl daemon-reload

# Enable and start service
echo "🚀 Enabling and starting service..."
sudo systemctl enable gotham-seed-server
sudo systemctl start gotham-seed-server

# Wait a moment for startup
sleep 5

# Check status
echo "📊 Checking service status..."
if sudo systemctl is-active --quiet gotham-seed-server; then
    echo "✅ Gotham City Seed Server is running!"
    
    # Try to get the onion address
    sleep 10  # Wait for Tor to generate the address
    if [ -f /var/lib/gotham-seed/hidden_service/hostname ]; then
        ONION_ADDRESS=$(sudo cat /var/lib/gotham-seed/hidden_service/hostname)
        echo "🧅 Your seed server .onion address: $ONION_ADDRESS"
        echo ""
        echo "📝 IMPORTANT: Save this address! You'll need it to configure clients."
        echo "   Add this address to your client's seed server list:"
        echo "   std::vector<std::string> seed_servers = {\"$ONION_ADDRESS\"};"
    else
        echo "⏳ Onion address not yet available. Check again in a few minutes:"
        echo "   sudo cat /var/lib/gotham-seed/hidden_service/hostname"
    fi
    
    echo ""
    echo "🔧 Management commands:"
    echo "   View logs:    sudo journalctl -u gotham-seed-server -f"
    echo "   Stop server:  sudo systemctl stop gotham-seed-server"
    echo "   Start server: sudo systemctl start gotham-seed-server"
    echo "   Restart:      sudo systemctl restart gotham-seed-server"
    echo "   Status:       sudo systemctl status gotham-seed-server"
    
else
    echo "❌ Failed to start seed server!"
    echo "   Check logs: sudo journalctl -u gotham-seed-server"
    exit 1
fi

echo ""
echo "🛡️ Security Recommendations:"
echo "   1. Configure firewall to only allow necessary ports"
echo "   2. Keep Tor updated: sudo apt update && sudo apt upgrade tor"
echo "   3. Monitor logs regularly for abuse"
echo "   4. Consider running multiple seed servers for redundancy"
echo "   5. Backup your configuration files"

echo ""
echo "✅ Deployment complete!"