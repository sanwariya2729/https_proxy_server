#include "policy_engine.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <arpa/inet.h>

namespace proxy {

PolicyEngine::PolicyEngine(const PolicyConfig& config) : config_(config) {}

bool PolicyEngine::load_from_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Parse key=value
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t\r\n"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

        if (key == "default_policy") {
            config_.default_allow = (value == "allow");
        } else if (key == "allow_domain") {
            config_.allowed_domains.insert(normalize_domain(value));
        } else if (key == "block_domain") {
            config_.blocked_domains.insert(normalize_domain(value));
        } else if (key == "allow_port") {
            config_.allowed_ports.insert(static_cast<uint16_t>(std::stoi(value)));
        } else if (key == "max_connections") {
            config_.max_connections = static_cast<size_t>(std::stoul(value));
        } else if (key == "max_queue_size") {
            config_.max_queue_size = static_cast<size_t>(std::stoul(value));
        } else if (key == "max_header_bytes") {
            config_.max_header_bytes = static_cast<size_t>(std::stoul(value));
        } else if (key == "header_timeout") {
            config_.header_timeout = std::stoi(value);
        } else if (key == "connect_timeout") {
            config_.connect_timeout = std::stoi(value);
        } else if (key == "upstream_timeout") {
            config_.upstream_timeout = std::stoi(value);
        }
    }

    return true;
}

std::string PolicyEngine::normalize_domain(const std::string& domain) const {
    std::string normalized = domain;

    // Convert to lowercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Remove trailing dot
    if (!normalized.empty() && normalized.back() == '.') {
        normalized.pop_back();
    }

    return normalized;
}

bool PolicyEngine::domain_matches(const std::string& host, const std::string& pattern) const {
    std::string norm_host = normalize_domain(host);

    // Exact match
    if (norm_host == pattern) {
        return true;
    }

    // Subdomain match: host ends with .pattern
    if (norm_host.length() > pattern.length() + 1) {
        size_t pos = norm_host.length() - pattern.length();
        if (norm_host[pos - 1] == '.' && norm_host.substr(pos) == pattern) {
            return true;
        }
    }

    return false;
}

PolicyDecision PolicyEngine::evaluate(const std::string& host, uint16_t port) const {
    // Validate host is not empty
    if (host.empty()) {
        return PolicyDecision::Deny;
    }

    // Check port allowlist
    if (!config_.allowed_ports.empty() &&
        config_.allowed_ports.find(port) == config_.allowed_ports.end()) {
        return PolicyDecision::Deny;
    }

    // Check blocklist first (takes precedence)
    for (const auto& blocked : config_.blocked_domains) {
        if (domain_matches(host, blocked)) {
            return PolicyDecision::Deny;
        }
    }

    // Check allowlist
    for (const auto& allowed : config_.allowed_domains) {
        if (domain_matches(host, allowed)) {
            return PolicyDecision::Allow;
        }
    }

    // Apply default policy
    return config_.default_allow ? PolicyDecision::Allow : PolicyDecision::Deny;
}

bool PolicyEngine::is_blocked_address_range(const std::string& ip) const {
    struct in_addr addr4;
    struct in6_addr addr6;

    // Try IPv4
    if (inet_pton(AF_INET, ip.c_str(), &addr4) == 1) {
        uint32_t addr = ntohl(addr4.s_addr);

        // Block loopback: 127.0.0.0/8
        if ((addr & 0xFF000000) == 0x7F000000) return true;

        // Block private: 10.0.0.0/8
        if ((addr & 0xFF000000) == 0x0A000000) return true;

        // Block private: 172.16.0.0/12
        if ((addr & 0xFFF00000) == 0xAC100000) return true;

        // Block private: 192.168.0.0/16
        if ((addr & 0xFFFF0000) == 0xC0A80000) return true;

        // Block link-local: 169.254.0.0/16
        if ((addr & 0xFFFF0000) == 0xA9FE0000) return true;

        // Block multicast: 224.0.0.0/4
        if ((addr & 0xF0000000) == 0xE0000000) return true;
    }
    // Try IPv6
    else if (inet_pton(AF_INET6, ip.c_str(), &addr6) == 1) {
        // Block loopback ::1
        if (IN6_IS_ADDR_LOOPBACK(&addr6)) return true;

        // Block link-local fe80::/10
        if (IN6_IS_ADDR_LINKLOCAL(&addr6)) return true;

        // Block multicast ff00::/8
        if (IN6_IS_ADDR_MULTICAST(&addr6)) return true;
    }

    return false;
}

bool PolicyEngine::is_address_allowed(const std::string& ip_address) const {
    return !is_blocked_address_range(ip_address);
}

} // namespace proxy
