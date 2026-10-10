#include "../include/policy_engine.hpp"
#include <iostream>
#include <cassert>

using namespace proxy;

void test_default_deny() {
    std::cout << "Test: Default deny policy... ";

    PolicyConfig config;
    config.default_allow = false;
    PolicyEngine engine(config);

    auto decision = engine.evaluate("unknown.com", 80);
    assert(decision == PolicyDecision::Deny);

    std::cout << "PASS" << std::endl;
}

void test_explicit_allow() {
    std::cout << "Test: Explicit allow rule... ";

    PolicyConfig config;
    config.default_allow = false;
    config.allowed_domains.insert("example.com");
    config.allowed_ports.insert(80);
    PolicyEngine engine(config);

    auto decision = engine.evaluate("example.com", 80);
    assert(decision == PolicyDecision::Allow);

    std::cout << "PASS" << std::endl;
}

void test_explicit_block() {
    std::cout << "Test: Explicit block rule... ";

    PolicyConfig config;
    config.default_allow = true;
    config.blocked_domains.insert("blocked.com");
    config.allowed_ports.insert(80);
    PolicyEngine engine(config);

    auto decision = engine.evaluate("blocked.com", 80);
    assert(decision == PolicyDecision::Deny);

    std::cout << "PASS" << std::endl;
}

void test_subdomain_matching() {
    std::cout << "Test: Subdomain matching... ";

    PolicyConfig config;
    config.default_allow = false;
    config.allowed_domains.insert("example.com");
    config.allowed_ports.insert(80);
    PolicyEngine engine(config);

    // Should allow subdomains
    auto decision1 = engine.evaluate("sub.example.com", 80);
    assert(decision1 == PolicyDecision::Allow);

    auto decision2 = engine.evaluate("deep.sub.example.com", 80);
    assert(decision2 == PolicyDecision::Allow);

    std::cout << "PASS" << std::endl;
}

void test_subdomain_spoof_prevention() {
    std::cout << "Test: Subdomain spoof prevention... ";

    PolicyConfig config;
    config.default_allow = false;
    config.allowed_domains.insert("example.com");
    config.allowed_ports.insert(80);
    PolicyEngine engine(config);

    // Should NOT match: example.com.attacker.com
    auto decision = engine.evaluate("example.com.attacker.com", 80);
    assert(decision == PolicyDecision::Deny);

    std::cout << "PASS" << std::endl;
}

void test_port_restriction() {
    std::cout << "Test: Port restriction... ";

    PolicyConfig config;
    config.default_allow = false;
    config.allowed_domains.insert("example.com");
    config.allowed_ports.insert(80);
    PolicyEngine engine(config);

    // Port 80 allowed
    auto decision1 = engine.evaluate("example.com", 80);
    assert(decision1 == PolicyDecision::Allow);

    // Port 443 not allowed
    auto decision2 = engine.evaluate("example.com", 443);
    assert(decision2 == PolicyDecision::Deny);

    std::cout << "PASS" << std::endl;
}

void test_block_takes_precedence() {
    std::cout << "Test: Block takes precedence over allow... ";

    PolicyConfig config;
    config.default_allow = true;
    config.allowed_domains.insert("example.com");
    config.blocked_domains.insert("example.com");
    config.allowed_ports.insert(80);
    PolicyEngine engine(config);

    // Block should win
    auto decision = engine.evaluate("example.com", 80);
    assert(decision == PolicyDecision::Deny);

    std::cout << "PASS" << std::endl;
}

void test_ssrf_protection_loopback() {
    std::cout << "Test: SSRF protection (loopback)... ";

    PolicyEngine engine;

    assert(!engine.is_address_allowed("127.0.0.1"));
    assert(!engine.is_address_allowed("127.0.0.2"));
    assert(!engine.is_address_allowed("::1"));

    std::cout << "PASS" << std::endl;
}

void test_ssrf_protection_private() {
    std::cout << "Test: SSRF protection (private)... ";

    PolicyEngine engine;

    assert(!engine.is_address_allowed("10.0.0.1"));
    assert(!engine.is_address_allowed("172.16.0.1"));
    assert(!engine.is_address_allowed("192.168.1.1"));
    assert(!engine.is_address_allowed("169.254.1.1"));

    std::cout << "PASS" << std::endl;
}

void test_ssrf_protection_public_allowed() {
    std::cout << "Test: SSRF protection (public allowed)... ";

    PolicyEngine engine;

    assert(engine.is_address_allowed("8.8.8.8"));
    assert(engine.is_address_allowed("1.1.1.1"));

    std::cout << "PASS" << std::endl;
}

int main() {
    std::cout << "=== Policy Engine Tests ===" << std::endl;

    test_default_deny();
    test_explicit_allow();
    test_explicit_block();
    test_subdomain_matching();
    test_subdomain_spoof_prevention();
    test_port_restriction();
    test_block_takes_precedence();
    test_ssrf_protection_loopback();
    test_ssrf_protection_private();
    test_ssrf_protection_public_allowed();

    std::cout << "\nAll policy tests passed!" << std::endl;
    return 0;
}
