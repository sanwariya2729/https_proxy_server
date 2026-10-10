# Secure HTTP Forward Proxy

A high-performance HTTP/1.1 forward proxy written in C++20 for Linux, featuring configurable destination policies, request validation, structured audit logging, and SSRF protection.

## 🎯 Project Overview

This proxy sits between clients and upstream web servers, validating requests, enforcing security policies, and providing detailed metrics. Built as a campus SDE internship portfolio project demonstrating systems programming, network security, and concurrent design.

## ✨ Features

- **HTTP/1.1 Forward Proxy** - Parses and forwards HTTP requests to upstream servers
- **Policy Engine** - Configurable allow/block lists with domain and port filtering
- **SSRF Protection** - Blocks requests to private/loopback addresses
- **Bounded Concurrency** - Worker thread pool with connection queue limits
- **Comprehensive Metrics** - Request counters, latency tracking (p50/p95/p99), structured logging
- **Resource Limits** - Configurable timeouts, header size limits, connection caps
- **Thread Safety** - Lock-free atomic counters for high-performance metrics

## 🏗️ Architecture

```
┌─────────┐         ┌──────────────┐         ┌──────────┐
│ Client  │────────▶│ Proxy Server │────────▶│ Upstream │
└─────────┘         └──────────────┘         └──────────┘
                           │
                           ├─ HTTP Parser
                           ├─ Policy Engine
                           ├─ Metrics
                           └─ Worker Pool
```

### Components

- **ProxyServer**: TCP listener, connection lifecycle, worker pool management
- **HttpParser**: HTTP/1.1 request parsing with validation
- **PolicyEngine**: Domain/port policy evaluation and SSRF checks
- **Metrics**: Thread-safe counters and latency measurements
- **Worker Pool**: Bounded thread pool with queue for concurrent request handling

## 🚀 Quick Start

### Prerequisites

- C++20 compiler (GCC 10+ or Clang 11+)
- CMake 3.20+
- Ninja build system
- Linux (tested on Ubuntu/Debian)

### Build

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt update
sudo apt install build-essential cmake ninja-build git

# Build the project
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run tests
./build/parser_tests
./build/policy_tests
```

### Run

```bash
# Start proxy with default settings (127.0.0.1:8080)
./build/proxy

# Custom configuration
./build/proxy --bind 127.0.0.1 --port 8080 --config config/policy.conf
```

## ⚙️ Configuration

Edit `config/policy.conf`:

```ini
# Default policy: allow or deny
default_policy=deny

# Allowed domains (exact match and subdomains)
allow_domain=example.com
allow_domain=httpbin.org

# Blocked domains (takes precedence)
block_domain=blocked.example

# Allowed ports
allow_port=80
allow_port=8080

# Resource limits
max_connections=50
max_queue_size=100
max_header_bytes=16384

# Timeouts (seconds)
header_timeout=10
connect_timeout=5
upstream_timeout=30
```

## 🧪 Testing

### Manual Testing

1. **Start a mock upstream server**:
```bash
# In terminal 1: Simple Python HTTP server
python3 -m http.server 9000
```

2. **Update policy to allow localhost:9000**:
```ini
allow_domain=localhost
allow_port=9000
```

3. **Start the proxy**:
```bash
# In terminal 2
./build/proxy --port 8080
```

4. **Send requests through proxy**:
```bash
# In terminal 3
curl -v --proxy http://127.0.0.1:8080 http://localhost:9000/
```

### Unit Tests

```bash
# HTTP parser tests
./build/parser_tests

# Policy engine tests  
./build/policy_tests

# Integration tests
./build/integration_tests
```

### Sanitizer Build

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
  
cmake --build build-asan
./build-asan/parser_tests
./build-asan/policy_tests
```

## 📊 Metrics

The proxy tracks:

- **Counters**: Total requests, allowed, blocked, parse failures, DNS failures, connect failures, upstream timeouts, capacity rejections
- **Latency**: p50, p95, p99 percentiles in milliseconds
- **Structured Logs**: Timestamp, destination, decision, status, duration, failure category

Example output:
```
[1728567890] dest=example.com:80 decision=allowed status=200 duration=45ms
[1728567891] dest=blocked.com:80 decision=blocked status=403 duration=1ms
```

Statistics summary on shutdown:
```
=== Proxy Statistics ===
Total requests: 1000
Allowed: 950
Blocked: 50
...
Latency (ms):
  p50: 42
  p95: 89
  p99: 156
========================
```

## 🔒 Security Features

### SSRF Protection

The proxy blocks requests to dangerous address ranges:
- Loopback: `127.0.0.0/8`, `::1`
- Private: `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`
- Link-local: `169.254.0.0/16`, `fe80::/10`
- Multicast: `224.0.0.0/4`, `ff00::/8`

### Domain Matching

- Exact match: `example.com` matches `example.com`
- Subdomain match: `example.com` matches `sub.example.com`
- **Prevents spoofing**: `example.com` does NOT match `example.com.attacker.com`

### Resource Limits

- Maximum header size: 16 KiB (prevents memory exhaustion)
- Bounded connection queue (prevents unbounded resource growth)
- Per-operation timeouts (prevents hanging connections)
- Worker pool cap (limits concurrency)

## 📈 Supported HTTP Subset

### Supported
- Methods: GET, HEAD, POST
- HTTP versions: 1.0, 1.1
- Absolute URI targets: `http://host:port/path`
- Connection: close semantics

### Limitations
- No HTTPS interception (CONNECT tunneling not implemented)
- No persistent connections (Connection: close only)
- No chunked transfer encoding
- No request bodies (yet)

These limitations are **documented and intentional** for a 10-day MVP scope.

## 🎓 Interview Preparation

### Key Questions

**Why use a worker pool instead of one thread per client?**
- Bounded pools limit resource growth and make overload behavior explicit and predictable

**What is SSRF and how does the proxy prevent it?**
- Server-Side Request Forgery lets attackers reach internal resources. The proxy validates both hostnames and resolved IPs against blocked ranges

**Why is domain suffix matching tricky?**
- Simple suffix checks accept `example.com.attacker.com`. Label-boundary matching prevents this

**Why use steady_clock for latency?**
- It's monotonic and unaffected by system time adjustments, making it reliable for elapsed time

**What are partial writes?**
- `send()` may send fewer bytes than requested; code must track and retry the remainder

**Why can throughput rise while p99 latency worsens?**
- Higher load increases queueing and contention, worsening tail latency even as more requests complete

## 📝 Project Structure

```
secure-http-proxy/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── config/
│   └── policy.conf         # Policy configuration
├── include/
│   ├── http_parser.hpp     # HTTP request parser
│   ├── metrics.hpp         # Metrics and logging
│   ├── policy_engine.hpp   # Policy evaluation
│   └── proxy_server.hpp    # Main server implementation
├── src/
│   ├── main.cpp            # Entry point
│   ├── http_parser.cpp
│   ├── metrics.cpp
│   ├── policy_engine.cpp
│   └── proxy_server.cpp
├── tests/
│   ├── parser_tests.cpp    # HTTP parser unit tests
│   ├── policy_tests.cpp    # Policy engine unit tests
│   └── integration_tests.cpp
└── docs/
    ├── architecture.md     # Detailed architecture
    └── benchmark-report.md # Performance measurements
```

## 🚦 Known Issues & Future Work

- No CONNECT tunneling for HTTPS (would need TLS interception)
- No request body handling yet
- No persistent connection support
- Integration tests are simplified (full E2E requires more infrastructure)
- Performance benchmarking needs load testing tool

## 📚 References

- [RFC 7230 - HTTP/1.1 Message Syntax](https://tools.ietf.org/html/rfc7230)
- [RFC 7231 - HTTP/1.1 Semantics](https://tools.ietf.org/html/rfc7231)
- [socket(2)](https://man7.org/linux/man-pages/man2/socket.2.html)
- [getaddrinfo(3)](https://man7.org/linux/man-pages/man3/getaddrinfo.3.html)

## 📄 License

Educational project for portfolio use.

## 👤 Author

Built as a network security internship portfolio project demonstrating C++20, systems programming, concurrent design, and security-first development practices.

---

**Built with**: C++20 • Linux • CMake • Ninja
