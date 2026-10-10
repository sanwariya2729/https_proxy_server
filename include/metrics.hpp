#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <mutex>
#include <vector>

namespace proxy {

struct RequestMetrics {
    std::string timestamp;
    std::string destination;
    std::string decision;
    int status_code{0};
    int64_t duration_ms{0};
    std::string failure_category;
};

class Metrics {
public:
    // Counters
    void increment_total_requests() { total_requests_.fetch_add(1, std::memory_order_relaxed); }
    void increment_allowed_requests() { allowed_requests_.fetch_add(1, std::memory_order_relaxed); }
    void increment_blocked_requests() { blocked_requests_.fetch_add(1, std::memory_order_relaxed); }
    void increment_parse_failures() { parse_failures_.fetch_add(1, std::memory_order_relaxed); }
    void increment_dns_failures() { dns_failures_.fetch_add(1, std::memory_order_relaxed); }
    void increment_connect_failures() { connect_failures_.fetch_add(1, std::memory_order_relaxed); }
    void increment_upstream_timeouts() { upstream_timeouts_.fetch_add(1, std::memory_order_relaxed); }
    void increment_rejected_capacity() { rejected_capacity_.fetch_add(1, std::memory_order_relaxed); }

    void set_active_connections(size_t count) { active_connections_.store(count, std::memory_order_relaxed); }
    size_t get_active_connections() const { return active_connections_.load(std::memory_order_relaxed); }

    // Latency tracking
    using TimePoint = std::chrono::steady_clock::time_point;
    TimePoint start_request() const { return std::chrono::steady_clock::now(); }

    int64_t end_request(TimePoint start) const {
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    }

    // Record completed request
    void record_request(const RequestMetrics& metrics);

    // Get statistics
    uint64_t get_total_requests() const { return total_requests_.load(std::memory_order_relaxed); }
    uint64_t get_allowed_requests() const { return allowed_requests_.load(std::memory_order_relaxed); }
    uint64_t get_blocked_requests() const { return blocked_requests_.load(std::memory_order_relaxed); }
    uint64_t get_parse_failures() const { return parse_failures_.load(std::memory_order_relaxed); }
    uint64_t get_dns_failures() const { return dns_failures_.load(std::memory_order_relaxed); }
    uint64_t get_connect_failures() const { return connect_failures_.load(std::memory_order_relaxed); }
    uint64_t get_upstream_timeouts() const { return upstream_timeouts_.load(std::memory_order_relaxed); }
    uint64_t get_rejected_capacity() const { return rejected_capacity_.load(std::memory_order_relaxed); }

    // Print summary
    void print_summary() const;

private:
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> allowed_requests_{0};
    std::atomic<uint64_t> blocked_requests_{0};
    std::atomic<uint64_t> parse_failures_{0};
    std::atomic<uint64_t> dns_failures_{0};
    std::atomic<uint64_t> connect_failures_{0};
    std::atomic<uint64_t> upstream_timeouts_{0};
    std::atomic<uint64_t> rejected_capacity_{0};
    std::atomic<size_t> active_connections_{0};

    mutable std::mutex latency_mutex_;
    std::vector<int64_t> latencies_;
};

// Global metrics instance
extern Metrics g_metrics;

} // namespace proxy
