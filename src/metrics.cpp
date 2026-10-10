#include "metrics.hpp"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <algorithm>

namespace proxy {

Metrics g_metrics;

void Metrics::record_request(const RequestMetrics& metrics) {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    latencies_.push_back(metrics.duration_ms);

    // Log structured output
    std::cout << "[" << metrics.timestamp << "] "
              << "dest=" << metrics.destination << " "
              << "decision=" << metrics.decision << " "
              << "status=" << metrics.status_code << " "
              << "duration=" << metrics.duration_ms << "ms";

    if (!metrics.failure_category.empty()) {
        std::cout << " failure=" << metrics.failure_category;
    }

    std::cout << std::endl;
}

void Metrics::print_summary() const {
    std::cout << "\n=== Proxy Statistics ===" << std::endl;
    std::cout << "Total requests: " << get_total_requests() << std::endl;
    std::cout << "Allowed: " << get_allowed_requests() << std::endl;
    std::cout << "Blocked: " << get_blocked_requests() << std::endl;
    std::cout << "Parse failures: " << get_parse_failures() << std::endl;
    std::cout << "DNS failures: " << get_dns_failures() << std::endl;
    std::cout << "Connect failures: " << get_connect_failures() << std::endl;
    std::cout << "Upstream timeouts: " << get_upstream_timeouts() << std::endl;
    std::cout << "Rejected (capacity): " << get_rejected_capacity() << std::endl;
    std::cout << "Active connections: " << get_active_connections() << std::endl;

    // Calculate latency percentiles
    std::lock_guard<std::mutex> lock(latency_mutex_);
    if (!latencies_.empty()) {
        auto sorted = latencies_;
        std::sort(sorted.begin(), sorted.end());

        size_t p50_idx = sorted.size() * 50 / 100;
        size_t p95_idx = sorted.size() * 95 / 100;
        size_t p99_idx = sorted.size() * 99 / 100;

        std::cout << "\nLatency (ms):" << std::endl;
        std::cout << "  p50: " << sorted[p50_idx] << std::endl;
        std::cout << "  p95: " << sorted[p95_idx] << std::endl;
        std::cout << "  p99: " << sorted[p99_idx] << std::endl;
    }
    std::cout << "========================\n" << std::endl;
}

} // namespace proxy
