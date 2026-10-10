#include "proxy_server.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

namespace proxy {

void Socket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

ProxyServer::ProxyServer(const std::string& bind_addr, uint16_t port, const PolicyEngine& policy)
    : bind_addr_(bind_addr), port_(port), policy_(policy) {}

ProxyServer::~ProxyServer() {
    stop();
}

bool ProxyServer::start() {
    // Create listening socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    listen_socket_ = Socket(listen_fd);

    // Set SO_REUSEADDR for easier restarts
    int opt = 1;
    if (setsockopt(listen_socket_.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
        return false;
    }

    // Bind to address
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, bind_addr_.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Invalid bind address: " << bind_addr_ << std::endl;
        return false;
    }

    if (bind(listen_socket_.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to " << bind_addr_ << ":" << port_
                  << " - " << strerror(errno) << std::endl;
        return false;
    }

    // Listen with bounded backlog
    const int backlog = 128;
    if (listen(listen_socket_.get(), backlog) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        return false;
    }

    running_ = true;

    std::cout << "Proxy server listening on " << bind_addr_ << ":" << port_ << std::endl;

    // Start worker threads
    size_t num_workers = policy_.get_config().max_connections;
    for (size_t i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&ProxyServer::worker_thread, this);
    }

    // Accept loop
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_socket_.get(),
                              reinterpret_cast<struct sockaddr*>(&client_addr),
                              &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (running_) {
                std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            }
            break;
        }

        // Check queue capacity
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (client_queue_.size() >= policy_.get_config().max_queue_size) {
                g_metrics.increment_rejected_capacity();
                ::close(client_fd);
                continue;
            }

            client_queue_.push(client_fd);
        }

        queue_cv_.notify_one();
    }

    return true;
}

void ProxyServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    listen_socket_.close();

    // Wake all workers
    queue_cv_.notify_all();

    // Join worker threads
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers_.clear();

    // Close any remaining clients
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!client_queue_.empty()) {
        ::close(client_queue_.front());
        client_queue_.pop();
    }

    g_metrics.print_summary();
}

void ProxyServer::worker_thread() {
    while (running_) {
        int client_fd = -1;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !client_queue_.empty() || !running_;
            });

            if (!running_) {
                break;
            }

            if (!client_queue_.empty()) {
                client_fd = client_queue_.front();
                client_queue_.pop();
            }
        }

        if (client_fd >= 0) {
            handle_client(client_fd);
        }
    }
}

void ProxyServer::handle_client(int client_fd) {
    Socket client_sock(client_fd);
    auto start_time = g_metrics.start_request();

    g_metrics.increment_total_requests();
    size_t active = g_metrics.get_active_connections() + 1;
    g_metrics.set_active_connections(active);

    RequestMetrics req_metrics;
    req_metrics.timestamp = std::to_string(std::time(nullptr));

    // Read request with timeout
    std::string buffer;
    if (!read_request(client_sock.get(), buffer, policy_.get_config().header_timeout)) {
        req_metrics.failure_category = "read_timeout";
        send_error_response(client_sock.get(), 408, "Request Timeout");
        req_metrics.duration_ms = g_metrics.end_request(start_time);
        g_metrics.record_request(req_metrics);
        g_metrics.set_active_connections(active - 1);
        return;
    }

    // Parse request
    HttpParser parser;
    ParseError parse_err;
    auto req_opt = parser.parse(buffer, parse_err);

    if (!req_opt) {
        g_metrics.increment_parse_failures();
        req_metrics.failure_category = "parse_error";
        send_error_response(client_sock.get(), 400, "Bad Request");
        req_metrics.duration_ms = g_metrics.end_request(start_time);
        g_metrics.record_request(req_metrics);
        g_metrics.set_active_connections(active - 1);
        return;
    }

    HttpRequest req = *req_opt;
    req_metrics.destination = req.host + ":" + std::to_string(req.port);

    // Evaluate policy
    PolicyDecision decision = policy_.evaluate(req.host, req.port);

    if (decision == PolicyDecision::Deny) {
        g_metrics.increment_blocked_requests();
        req_metrics.decision = "blocked";
        req_metrics.status_code = 403;
        send_error_response(client_sock.get(), 403, "Forbidden");
        req_metrics.duration_ms = g_metrics.end_request(start_time);
        g_metrics.record_request(req_metrics);
        g_metrics.set_active_connections(active - 1);
        return;
    }

    g_metrics.increment_allowed_requests();
    req_metrics.decision = "allowed";

    // Forward to upstream
    if (!forward_to_upstream(req, client_sock.get())) {
        req_metrics.failure_category = "upstream_error";
    }

    req_metrics.status_code = 200;
    req_metrics.duration_ms = g_metrics.end_request(start_time);
    g_metrics.record_request(req_metrics);
    g_metrics.set_active_connections(active - 1);
}

bool ProxyServer::read_request(int fd, std::string& buffer, int timeout_sec) {
    set_socket_timeout(fd, timeout_sec);

    char chunk[4096];
    const size_t max_size = policy_.get_config().max_header_bytes;

    while (buffer.size() < max_size) {
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (n == 0) {
            return false;
        }

        buffer.append(chunk, static_cast<size_t>(n));

        if (HttpParser::has_complete_headers(buffer)) {
            return true;
        }
    }

    return false;
}

bool ProxyServer::forward_to_upstream(const HttpRequest& req, int client_fd) {
    Socket upstream_sock;

    if (!connect_upstream(req.host, req.port, policy_.get_config().connect_timeout, upstream_sock)) {
        send_error_response(client_fd, 502, "Bad Gateway");
        return false;
    }

    // Build and send upstream request
    std::string upstream_req = build_upstream_request(req);
    const char* data = upstream_req.data();
    size_t remaining = upstream_req.size();

    while (remaining > 0) {
        ssize_t sent = send(upstream_sock.get(), data, remaining, 0);

        if (sent < 0) {
            if (errno == EINTR) continue;
            send_error_response(client_fd, 502, "Bad Gateway");
            return false;
        }

        data += sent;
        remaining -= static_cast<size_t>(sent);
    }

    // Relay response back to client
    char buffer[8192];
    while (true) {
        ssize_t n = recv(upstream_sock.get(), buffer, sizeof(buffer), 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }

        if (n == 0) {
            break;
        }

        const char* ptr = buffer;
        size_t left = static_cast<size_t>(n);

        while (left > 0) {
            ssize_t sent = send(client_fd, ptr, left, 0);

            if (sent < 0) {
                if (errno == EINTR) continue;
                return false;
            }

            ptr += sent;
            left -= static_cast<size_t>(sent);
        }
    }

    return true;
}

bool ProxyServer::connect_upstream(const std::string& host, uint16_t port,
                                   int timeout_sec, Socket& sock) {
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int ret = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);

    if (ret != 0) {
        g_metrics.increment_dns_failures();
        return false;
    }

    bool connected = false;

    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        // Validate address against SSRF policy
        char addr_str[INET6_ADDRSTRLEN];
        void* addr_ptr = nullptr;

        if (rp->ai_family == AF_INET) {
            addr_ptr = &(reinterpret_cast<struct sockaddr_in*>(rp->ai_addr)->sin_addr);
        } else if (rp->ai_family == AF_INET6) {
            addr_ptr = &(reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr)->sin6_addr);
        }

        if (addr_ptr && inet_ntop(rp->ai_family, addr_ptr, addr_str, sizeof(addr_str))) {
            if (!policy_.is_address_allowed(addr_str)) {
                continue;
            }
        }

        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        sock = Socket(fd);
        set_socket_timeout(fd, timeout_sec);

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            connected = true;
            break;
        }

        sock.close();
    }

    freeaddrinfo(result);

    if (!connected) {
        g_metrics.increment_connect_failures();
    }

    return connected;
}

void ProxyServer::send_error_response(int fd, int status_code, const std::string& message) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << message << "\r\n"
        << "Content-Type: text/plain\r\n"
        << "Connection: close\r\n"
        << "Content-Length: " << message.length() << "\r\n"
        << "\r\n"
        << message;

    std::string response = oss.str();
    send(fd, response.data(), response.size(), 0);
}

bool ProxyServer::set_socket_timeout(int fd, int seconds) {
    struct timeval tv{};
    tv.tv_sec = seconds;
    tv.tv_usec = 0;

    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

bool ProxyServer::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace proxy
