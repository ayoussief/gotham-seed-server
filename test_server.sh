#!/bin/bash

echo "🧪 Testing Gotham City Seed Server with crash recovery..."

# Function to handle server crashes
run_server() {
    local attempt=1
    while [ $attempt -le 3 ]; do
        echo "🚀 Starting server (attempt $attempt)..."
        
        # Run server and capture exit code
        timeout 60s ./gotham-seed-server --verbose
        exit_code=$?
        
        echo "Server exited with code: $exit_code"
        
        if [ $exit_code -eq 0 ]; then
            echo "✅ Server completed successfully"
            break
        elif [ $exit_code -eq 124 ]; then
            echo "⏰ Server timeout (60s) - test completed"
            break
        else
            echo "❌ Server crashed (exit code: $exit_code)"
            echo "🔄 Restarting server in 2 seconds..."
            sleep 2
        fi
        
        attempt=$((attempt + 1))
    done
    
    if [ $attempt -gt 3 ]; then
        echo "❌ Server failed after 3 attempts"
        return 1
    fi
    
    return 0
}

# Change to build directory
cd "$(dirname "$0")/build" || exit 1

# Run the test
run_server

echo "🏁 Test completed"