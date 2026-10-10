#include "proxy_server.hpp"
#include "policy_engine.hpp"
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> g_shutdown{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_shutdown = true;
    }
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string bind_addr = "127.0.0.1";
    uint16_t port = 8080;
    std::string config_file = "config/policy.conf";

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--bind" && i + 1 < argc) {
            bind_addr = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --bind <addr>    Bind address (default: 127.0.0.1)\n"
                      << "  --port <port>    Port number (default: 8080)\n"
                      << "  --config <file>  Policy config file (default: config/policy.conf)\n"
                      << "  --help           Show this help message\n";
            return 0;
        }
    }

    // Load policy
    proxy::PolicyEngine policy;
    if (!policy.load_from_file(config_file)) {
        std::cerr << "Warning: Could not load policy file " << config_file
                  << ", using default policy" << std::endl;
    }

    std::cout << "Loaded policy: "
              << (policy.get_config().default_allow ? "default-allow" : "default-deny")
              << std::endl;
    std::cout << "Allowed domains: " << policy.get_config().allowed_domains.size() << std::endl;
    std::cout << "Blocked domains: " << policy.get_config().blocked_domains.size() << std::endl;
    std::cout << "Allowed ports: " << policy.get_config().allowed_ports.size() << std::endl;

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create and start server
    proxy::ProxyServer server(bind_addr, port, policy);

    if (!server.start()) {
        std::cerr << "Failed to start proxy server" << std::endl;
        return 1;
    }

    return 0;
}
