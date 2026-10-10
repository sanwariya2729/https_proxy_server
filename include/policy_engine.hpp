#pragma once

#include <string>
#include <set>
#include <cstdint>

namespace proxy {

enum class PolicyDecision {
    Allow,
    Deny
};

struct PolicyConfig {
    bool default_allow{false};
    std::set<std::string> allowed_domains;
    std::set<std::string> blocked_domains;
    std::set<uint16_t> allowed_ports;

    // Resource limits
    size_t max_connections{50};
    size_t max_queue_size{100};
    size_t max_header_bytes{16384};

    // Timeouts (seconds)
    int header_timeout{10};
    int connect_timeout{5};
    int upstream_timeout{30};
};

class PolicyEngine {
public:
    PolicyEngine() = default;
    explicit PolicyEngine(const PolicyConfig& config);

    // Load policy from configuration file
    bool load_from_file(const std::string& filepath);

    // Evaluate policy for given host and port
    PolicyDecision evaluate(const std::string& host, uint16_t port) const;

    // Get current configuration
    const PolicyConfig& get_config() const { return config_; }

    // Validate if address is allowed (prevents SSRF)
    bool is_address_allowed(const std::string& ip_address) const;

private:
    PolicyConfig config_;

    std::string normalize_domain(const std::string& domain) const;
    bool domain_matches(const std::string& host, const std::string& pattern) const;
    bool is_blocked_address_range(const std::string& ip) const;
};

} // namespace proxy
