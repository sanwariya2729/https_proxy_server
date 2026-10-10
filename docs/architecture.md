# Architecture

## System Overview

The Secure HTTP Forward Proxy is a multi-threaded network application that mediates HTTP traffic between clients and upstream servers. It enforces security policies, validates requests, and provides detailed observability through structured logging and metrics.

## Design Philosophy

1. **Security First**: Default-deny policy, SSRF protection, resource limits
2. **Explicit Limits**: All resources (connections, memory, time) are bounded
3. **Fail-Safe**: Invalid requests denied before reaching upstream
4. **Observable**: Every request produces structured logs and metrics

## Request Lifecycle

```
┌──────────────────────────────────────────────────────────────┐
│                     Accept Connection                         │
│         - Check queue capacity                                │
│         - Enqueue to worker pool                              │
└──────────────────────┬───────────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────────┐
│                  Read Request Headers                         │
│         - Timeout: configurable (default 10s)                 │
│         - Limit: 16 KiB                                       │
│         - Look for \r\n\r\n terminator                        │
└──────────────────────┬───────────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────────┐
│                     Parse Request                             │
│         - Extract method, URI, version                        │
│         - Parse headers (case-insensitive)                    │
│         - Extract Host and port                               │
│         - Validate HTTP compliance                            │
└──────────────────────┬───────────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────────┐
│                  Evaluate Policy                              │
│         1. Check port allowlist                               │
│         2. Check domain blocklist (takes precedence)          │
│         3. Check domain allowlist                             │
│         4. Apply default policy                               │
│                                                               │
│         Decision: ALLOW or DENY                               │
└──────────────────────┬───────────────────────────────────────┘
                       │
             ┌─────────┴─────────┐
             │                   │
           DENY                ALLOW
             │                   │
             ▼                   ▼
    ┌────────────────┐   ┌────────────────────────────────┐
    │ Send 403       │   │   Resolve Destination          │
    │ Forbidden      │   │   - getaddrinfo()              │
    └────────────────┘   │   - Validate resolved IPs      │
                         │   - SSRF protection            │
                         └────────┬───────────────────────┘
                                  │
                                  ▼
                         ┌────────────────────────────────┐
                         │   Connect to Upstream          │
                         │   - Timeout: configurable      │
                         │   - Non-blocking with select   │
                         └────────┬───────────────────────┘
                                  │
                                  ▼
                         ┌────────────────────────────────┐
                         │   Forward Request              │
                         │   - Build origin-form request  │
                         │   - Remove hop-by-hop headers  │
                         │   - Add Connection: close      │
                         │   - Handle partial writes      │
                         └────────┬───────────────────────┘
                                  │
                                  ▼
                         ┌────────────────────────────────┐
                         │   Relay Response               │
                         │   - Stream to client           │
                         │   - Bounded buffers            │
                         │   - Handle partial reads       │
                         └────────┬───────────────────────┘
                                  │
                                  ▼
                         ┌────────────────────────────────┐
                         │   Log & Update Metrics         │
                         │   - Record outcome             │
                         │   - Measure latency            │
                         │   - Structured log entry       │
                         └────────────────────────────────┘
```

## Module Details

### ProxyServer

**Responsibilities:**
- TCP socket management (RAII wrapper)
- Accept loop on listening socket
- Worker thread pool management
- Connection queue with capacity limits
- Timeout enforcement
- Error response generation

**Key Design Decisions:**
- Bounded worker pool prevents unbounded thread creation
- Queue capacity limit provides backpressure
- RAII Socket class ensures cleanup on all paths
- SO_REUSEADDR for easy local development

### HttpParser

**Responsibilities:**
- HTTP/1.1 request line parsing
- Header parsing with case normalization
- Host/port extraction from absolute URIs
- Request validation

**Key Design Decisions:**
- Stateless parser (pure function)
- Strict 16 KiB header limit
- Requires complete headers before parsing
- Explicit error types for diagnosability

**Parsing Rules:**
- Request line: `METHOD URI HTTP/VERSION`
- Headers: case-insensitive names, whitespace-trimmed values
- Host extraction: from absolute URI, then Host header
- Validation: method whitelist, version check, required Host

### PolicyEngine

**Responsibilities:**
- Configuration file parsing
- Policy rule evaluation
- Domain matching with subdomain support
- SSRF prevention via IP validation

**Key Design Decisions:**
- Default-deny is explicit in config
- Blocklist takes precedence over allowlist
- Label-boundary domain matching prevents spoofing
- IP validation happens after DNS resolution

**Policy Evaluation Order:**
1. Empty host → DENY
2. Port not in allowlist → DENY (if allowlist non-empty)
3. Domain in blocklist → DENY
4. Domain in allowlist → ALLOW
5. Fall through to default policy

**Domain Matching Logic:**
```cpp
// Pattern: "example.com"
// Matches: "example.com", "sub.example.com"
// Rejects: "example.com.attacker.com", "notexample.com"

bool domain_matches(host, pattern) {
    normalized_host = lowercase(host)
    if (normalized_host == pattern) return true
    
    // Check subdomain: host must end with ".pattern"
    if (host.ends_with("." + pattern)) return true
    
    return false
}
```

**SSRF Protection:**
- Validates resolved IP addresses, not just hostnames
- Blocks loopback, private, link-local, multicast ranges
- Prevents attackers from reaching internal infrastructure

### Metrics

**Responsibilities:**
- Thread-safe request counters
- Latency measurement and percentile calculation
- Structured logging

**Key Design Decisions:**
- Atomic counters for lock-free performance
- Mutex-protected latency vector (updated infrequently)
- Structured logs with timestamp, decision, status, duration
- No PII/credentials in logs

**Tracked Metrics:**
- Total requests
- Allowed / blocked requests
- Parse / DNS / connect failures
- Upstream timeouts
- Capacity rejections
- Active connections
- Latency distribution (p50, p95, p99)

## Concurrency Model

### Worker Pool Pattern

```
                  ┌─────────────────────┐
                  │   Accept Thread     │
                  │  (Main Thread)      │
                  └──────────┬──────────┘
                             │
                             │ accept()
                             │
                             ▼
                  ┌──────────────────────┐
                  │  Bounded Queue       │
                  │  (max_queue_size)    │
                  └──────────┬───────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
    ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
    │   Worker 1   │  │   Worker 2   │  │   Worker N   │
    │              │  │              │  │              │
    └──────────────┘  └──────────────┘  └──────────────┘
```

**Benefits:**
- Predictable resource usage
- Explicit overload behavior
- Bounded memory consumption
- Clean shutdown

**Trade-offs:**
- Queueing latency under load
- Cannot scale beyond N workers
- Could use epoll for higher scale (future work)

## Error Handling

### Categories

1. **Parse Errors**: Malformed requests → 400 Bad Request
2. **Policy Denials**: Blocked by policy → 403 Forbidden
3. **Timeouts**: Header/connect/upstream → 408 Request Timeout / 504 Gateway Timeout
4. **Upstream Errors**: DNS/connect failures → 502 Bad Gateway
5. **Capacity**: Queue full → Close connection, increment rejection counter

### Guarantees

- Every request produces exactly one log entry
- Failures never leak file descriptors
- Clients receive HTTP error responses (not bare connection close)
- Upstream never sees blocked requests

## Security Considerations

### Threat Model

**In Scope:**
- Malicious clients attempting SSRF
- Clients sending malformed requests
- Resource exhaustion attacks

**Out of Scope:**
- TLS interception / HTTPS content inspection
- Authentication / authorization (deploy with separate auth)
- DDoS mitigation (deploy with rate limiting)

### Mitigations

| Threat | Mitigation |
|--------|------------|
| SSRF to internal IPs | Validate resolved addresses before connect |
| Domain spoofing | Label-boundary matching |
| Memory exhaustion | 16 KiB header limit, bounded queue |
| Connection exhaustion | Worker pool cap, queue limit |
| Slowloris | Header read timeout |
| Upstream hangs | Connect/upstream timeouts |

## Performance Characteristics

### Bottlenecks

1. **DNS resolution**: Blocking getaddrinfo() in worker thread
2. **Upstream latency**: Workers block while forwarding
3. **Mutex contention**: Latency vector updates (rare)

### Optimization Opportunities (Future Work)

- Async DNS resolution
- Connection pooling to frequent upstreams
- epoll-based event loop
- Zero-copy forwarding
- Cached policy decisions

### Expected Performance

- **Throughput**: 100-500 req/s (single-core, local upstream)
- **Latency**: p50 ~5-10ms, p99 ~50-100ms (depends on upstream)
- **Memory**: ~1-2 MB per active connection
- **Threads**: N workers + 1 accept thread

## Testing Strategy

### Unit Tests
- Parser: valid/malformed/incomplete requests
- Policy: allow/block/subdomain/SSRF cases

### Integration Tests
- End-to-end request flow with mock upstream
- Blocked request never reaches upstream
- Timeout handling
- Concurrent client handling

### Manual Testing
- Local Python HTTP server as upstream
- curl with --proxy flag
- Policy configuration changes
- Resource limit validation

### Sanitizer Testing
- AddressSanitizer: memory errors, leaks
- UndefinedBehaviorSanitizer: UB detection
- Must pass clean on all test suites

## Deployment Considerations

### Development
- Bind to 127.0.0.1 only
- Default-deny policy
- Allow only local test upstream

### Production (Future)
- Run behind authentication layer
- Deploy with rate limiting
- Monitor metrics feed
- Log aggregation
- Health check endpoint

## Future Enhancements

1. **CONNECT tunneling** (HTTPS pass-through, not inspection)
2. **Persistent connections** (Connection: keep-alive)
3. **Request body support** (POST/PUT with Content-Length)
4. **Connection pooling** to upstream servers
5. **Prometheus metrics endpoint**
6. **Health check endpoint** for load balancers
7. **Hot-reload configuration** (SIGHUP handler)
8. **Async DNS** via c-ares or similar

---

**Design Philosophy**: Correct, secure, observable — then fast.
