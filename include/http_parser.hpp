#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace proxy {

enum class ParseError {
    None,
    Incomplete,
    MalformedRequestLine,
    InvalidHeader,
    MissingHost,
    HeaderTooLarge,
    UnsupportedMethod,
    UnsupportedVersion,
    InvalidURI
};

struct HttpRequest {
    std::string method;
    std::string target;      // Full URI for forward proxy
    std::string path;        // Path component
    std::string version;
    std::string host;
    uint16_t port{80};

    // Case-insensitive header storage
    std::unordered_map<std::string, std::string> headers;

    bool is_valid() const {
        return !method.empty() && !target.empty() && !host.empty();
    }
};

class HttpParser {
public:
    // Maximum header size (16 KiB)
    static constexpr size_t MAX_HEADER_SIZE = 16384;

    // Parse HTTP request from buffer
    // Returns parsed request on success, or error code
    std::optional<HttpRequest> parse(const std::string& buffer, ParseError& error);

    // Check if buffer contains complete headers (CRLF CRLF)
    static bool has_complete_headers(const std::string& buffer);

private:
    bool parse_request_line(const std::string& line, HttpRequest& req, ParseError& error);
    bool parse_header_line(const std::string& line, HttpRequest& req);
    bool extract_host_port(const std::string& uri, HttpRequest& req);
    std::string normalize_header_name(const std::string& name) const;
    bool is_valid_header_name(const std::string& name) const;
};

// Helper to construct upstream request
std::string build_upstream_request(const HttpRequest& req);

} // namespace proxy
