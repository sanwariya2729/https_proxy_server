#pragma once

#include "http_parser.hpp"
#include "policy_engine.hpp"
#include "metrics.hpp"
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace proxy {

// RAII socket wrapper
class Socket {
public:
    Socket() : fd_(-1) {}
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { close(); }

    // Move only
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const { return fd_; }
    bool is_valid() const { return fd_ >= 0; }
    void close();
    int release() { int fd = fd_; fd_ = -1; return fd; }

private:
    int fd_;
};

class ProxyServer {
public:
    ProxyServer(const std::string& bind_addr, uint16_t port, const PolicyEngine& policy);
    ~ProxyServer();

    // Start the server
    bool start();

    // Stop the server gracefully
    void stop();

    // Check if server is running
    bool is_running() const { return running_.load(); }

private:
    std::string bind_addr_;
    uint16_t port_;
    PolicyEngine policy_;
    std::atomic<bool> running_{false};
    Socket listen_socket_;

    // Worker pool
    std::vector<std::thread> workers_;
    std::queue<int> client_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Thread pool management
    void worker_thread();
    void handle_client(int client_fd);

    // Request processing
    bool read_request(int fd, std::string& buffer, int timeout_sec);
    bool forward_to_upstream(const HttpRequest& req, int client_fd);
    bool connect_upstream(const std::string& host, uint16_t port, int timeout_sec, Socket& sock);
    void send_error_response(int fd, int status_code, const std::string& message);

    // Helper methods
    bool set_socket_timeout(int fd, int seconds);
    bool set_nonblocking(int fd);
};

} // namespace proxy
