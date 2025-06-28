#include <iostream>
#include <csignal>
#include <cstring>
#include <getopt.h>
#include <atomic>
#include <thread>
#include <chrono>

#include "seed_server.h"

// Global server instance for signal handling
std::unique_ptr<SeedServer> g_server;
std::atomic<bool> g_shutdown_requested{false};

void signalHandler(int signal) {
    if (signal == SIGSEGV || signal == SIGABRT) {
        std::cout << "\n⚠️ Tor crash detected (signal " << signal << ") - continuing operation..." << std::endl;
        return; // Don't shutdown on Tor crashes
    }
    
    std::cout << "\n🛑 Received signal " << signal << " - initiating graceful shutdown..." << std::endl;
    g_shutdown_requested = true;
    
    if (g_server) {
        g_server->stop();
    }
}

void printUsage(const char* program_name) {
    std::cout << "Gotham City Seed Server v1.0.0\n" << std::endl;
    std::cout << "Usage: " << program_name << " [OPTIONS]\n" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port PORT              Port to listen on (default: 12345)" << std::endl;
    std::cout << "  -m, --max-peers COUNT        Maximum peers to track (default: 500)" << std::endl;
    std::cout << "  -c, --cleanup-interval SEC   Cleanup interval in seconds (default: 180)" << std::endl;
    std::cout << "  -r, --rate-limit COUNT       Max requests per minute per peer (default: 60)" << std::endl;
    std::cout << "  -d, --data-dir PATH          Data directory for Tor config (default: ~/.gotham-seed)" << std::endl;
    std::cout << "  -v, --verbose                Enable verbose logging" << std::endl;
    std::cout << "  -h, --help                   Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << "                           # Run with default settings" << std::endl;
    std::cout << "  " << program_name << " --port 8080 --verbose     # Custom port with verbose logging" << std::endl;
    std::cout << "  " << program_name << " --max-peers 1000          # Support up to 1000 peers" << std::endl;
    std::cout << std::endl;
    std::cout << "The seed server helps Gotham City nodes discover peers while maintaining privacy." << std::endl;
    std::cout << "It operates over Tor and uses the GCTY protocol for secure communication." << std::endl;
}

void printBanner() {
    std::cout << R"(
    ╔══════════════════════════════════════════════════════════════╗
    ║                                                              ║
    ║              🦇 GOTHAM CITY SEED SERVER 🦇                   ║
    ║                                                              ║
    ║              Privacy-First Peer Discovery                    ║
    ║                                                              ║
    ╚══════════════════════════════════════════════════════════════╝
)" << std::endl;
}

int main(int argc, char* argv[]) {
    printBanner();
    
    // Parse command line arguments
    SeedServer::Config config;
    
    static struct option long_options[] = {
        {"port",             required_argument, 0, 'p'},
        {"max-peers",        required_argument, 0, 'm'},
        {"cleanup-interval", required_argument, 0, 'c'},
        {"rate-limit",       required_argument, 0, 'r'},
        {"data-dir",         required_argument, 0, 'd'},
        {"verbose",          no_argument,       0, 'v'},
        {"help",             no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "p:m:c:r:d:vh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'p':
                config.port = std::atoi(optarg);
                if (config.port <= 0 || config.port > 65535) {
                    std::cerr << "❌ Invalid port: " << optarg << std::endl;
                    return 1;
                }
                break;
                
            case 'm':
                config.max_peers = std::atoi(optarg);
                if (config.max_peers <= 0) {
                    std::cerr << "❌ Invalid max peers: " << optarg << std::endl;
                    return 1;
                }
                break;
                
            case 'c':
                config.cleanup_interval_seconds = std::atoi(optarg);
                if (config.cleanup_interval_seconds <= 0) {
                    std::cerr << "❌ Invalid cleanup interval: " << optarg << std::endl;
                    return 1;
                }
                break;
                
            case 'r':
                config.rate_limit_per_minute = std::atoi(optarg);
                if (config.rate_limit_per_minute <= 0) {
                    std::cerr << "❌ Invalid rate limit: " << optarg << std::endl;
                    return 1;
                }
                break;
                
            case 'd':
                config.data_directory = optarg;
                break;
                
            case 'v':
                config.verbose = true;
                break;
                
            case 'h':
                printUsage(argv[0]);
                return 0;
                
            case '?':
                std::cerr << "❌ Unknown option. Use --help for usage information." << std::endl;
                return 1;
                
            default:
                break;
        }
    }
    
    // Display configuration
    std::cout << "🔧 Configuration:" << std::endl;
    std::cout << "   Port: " << config.port << std::endl;
    std::cout << "   Max Peers: " << config.max_peers << std::endl;
    std::cout << "   Cleanup Interval: " << config.cleanup_interval_seconds << "s" << std::endl;
    std::cout << "   Rate Limit: " << config.rate_limit_per_minute << " req/min" << std::endl;
    std::cout << "   Data Directory: " << config.data_directory << std::endl;
    std::cout << "   Verbose: " << (config.verbose ? "enabled" : "disabled") << std::endl;
    std::cout << std::endl;
    
    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGQUIT, signalHandler);
    signal(SIGSEGV, signalHandler);  // Handle segfaults
    signal(SIGABRT, signalHandler);  // Handle aborts
    
    try {
        // Create and start server
        g_server = std::make_unique<SeedServer>(config);
        
        std::cout << "🚀 Starting Gotham City Seed Server..." << std::endl;
        
        if (!g_server->start()) {
            std::cerr << "❌ Failed to start seed server!" << std::endl;
            return 1;
        }
        
        std::cout << "✅ Seed server started successfully!" << std::endl;
        std::cout << "🧅 Onion Address: " << g_server->getOnionAddress() << std::endl;
        std::cout << "🔌 Listening on port: " << config.port << std::endl;
        std::cout << std::endl;
        std::cout << "🛡️ Privacy Features:" << std::endl;
        std::cout << "   🎭 No user tracking or logging" << std::endl;
        std::cout << "   🔒 Tor-only operation for maximum privacy" << std::endl;
        std::cout << "   🌱 GCTY protocol for network isolation" << std::endl;
        std::cout << "   ⚡ Automatic cleanup of inactive peers" << std::endl;
        std::cout << std::endl;
        std::cout << "📊 Use Ctrl+C to view stats and shutdown gracefully" << std::endl;
        std::cout << "================================================================" << std::endl;
        
        // Main loop - wait for shutdown signal
        while (!g_shutdown_requested && g_server->isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Print stats every 60 seconds if verbose
            static auto last_stats = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (config.verbose && 
                std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count() >= 60) {
                std::cout << "\n📊 Server Stats:\n" << g_server->getStats() << std::endl;
                last_stats = now;
            }
        }
        
        // Print final stats
        std::cout << "\n📊 Final Server Statistics:" << std::endl;
        std::cout << g_server->getStats() << std::endl;
        
        std::cout << "🛑 Shutting down gracefully..." << std::endl;
        g_server->stop();
        g_server.reset();
        
        std::cout << "✅ Gotham City Seed Server stopped successfully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Unknown fatal error occurred" << std::endl;
        return 1;
    }
    
    return 0;
}