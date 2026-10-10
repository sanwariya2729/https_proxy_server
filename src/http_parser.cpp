#include "http_parser.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace proxy {

bool HttpParser::has_complete_headers(const std::string& buffer) {
    return buffer.find("\r\n\r\n") != std::string::npos;
}

std::string HttpParser::normalize_header_name(const std::string& name) const {
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return normalized;
}

bool HttpParser::is_valid_header_name(const std::string& name) const {
    if (name.empty()) return false;

    for (char c : name) {
        if (std::iscntrl(static_cast<unsigned char>(c)) || c == ':') {
            return false;
        }
    }
    return true;
}

std::optional<HttpRequest> HttpParser::parse(const std::string& buffer, ParseError& error) {
    error = ParseError::None;

    // Check size limit
    if (buffer.size() > MAX_HEADER_SIZE) {
        error = ParseError::HeaderTooLarge;
        return std::nullopt;
    }

    // Check for complete headers
    if (!has_complete_headers(buffer)) {
        error = ParseError::Incomplete;
        return std::nullopt;
    }

    HttpRequest req;

    // Find end of headers
    size_t headers_end = buffer.find("\r\n\r\n");
    std::string headers_section = buffer.substr(0, headers_end);

    // Split into lines
    std::istringstream stream(headers_section);
    std::string line;
    bool first_line = true;

    while (std::getline(stream, line)) {
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (first_line) {
            if (!parse_request_line(line, req, error)) {
                return std::nullopt;
            }
            first_line = false;
        } else {
            if (!parse_header_line(line, req)) {
                error = ParseError::InvalidHeader;
                return std::nullopt;
            }
        }
    }

    // Validate Host header
    if (req.host.empty()) {
        error = ParseError::MissingHost;
        return std::nullopt;
    }

    return req;
}

bool HttpParser::parse_request_line(const std::string& line, HttpRequest& req, ParseError& error) {
    std::istringstream iss(line);

    if (!(iss >> req.method >> req.target >> req.version)) {
        error = ParseError::MalformedRequestLine;
        return false;
    }

    // Validate method
    if (req.method != "GET" && req.method != "HEAD" && req.method != "POST") {
        error = ParseError::UnsupportedMethod;
        return false;
    }

    // Validate version
    if (req.version != "HTTP/1.1" && req.version != "HTTP/1.0") {
        error = ParseError::UnsupportedVersion;
        return false;
    }

    // Extract host and port from target
    if (!extract_host_port(req.target, req)) {
        error = ParseError::InvalidURI;
        return false;
    }

    return true;
}

bool HttpParser::parse_header_line(const std::string& line, HttpRequest& req) {
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        return line.empty(); // Empty line is OK
    }

    std::string name = line.substr(0, colon_pos);
    std::string value = line.substr(colon_pos + 1);

    // Trim whitespace from value
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t\r\n") + 1);

    if (!is_valid_header_name(name)) {
        return false;
    }

    std::string norm_name = normalize_header_name(name);
    req.headers[norm_name] = value;

    // Handle Host header specially
    if (norm_name == "host") {
        // Extract host and port if not already set from URI
        if (req.host.empty()) {
            size_t port_pos = value.find(':');
            if (port_pos != std::string::npos) {
                req.host = value.substr(0, port_pos);
                req.port = static_cast<uint16_t>(std::stoi(value.substr(port_pos + 1)));
            } else {
                req.host = value;
            }
        }
    }

    return true;
}

bool HttpParser::extract_host_port(const std::string& uri, HttpRequest& req) {
    // For forward proxy, target should be absolute URI: http://host:port/path
    if (uri.find("http://") == 0) {
        size_t host_start = 7; // After "http://"
        size_t path_start = uri.find('/', host_start);

        std::string host_port;
        if (path_start != std::string::npos) {
            host_port = uri.substr(host_start, path_start - host_start);
            req.path = uri.substr(path_start);
        } else {
            host_port = uri.substr(host_start);
            req.path = "/";
        }

        // Extract host and port
        size_t port_pos = host_port.find(':');
        if (port_pos != std::string::npos) {
            req.host = host_port.substr(0, port_pos);
            req.port = static_cast<uint16_t>(std::stoi(host_port.substr(port_pos + 1)));
        } else {
            req.host = host_port;
            req.port = 80;
        }

        return true;
    }

    // For regular requests, path only (not typical for forward proxy)
    req.path = uri;
    return false; // Will need to get host from Host header
}

std::string build_upstream_request(const HttpRequest& req) {
    std::ostringstream oss;

    // Request line with origin-form (path only, not full URI)
    oss << req.method << " " << req.path << " " << req.version << "\r\n";

    // Host header
    oss << "Host: " << req.host;
    if (req.port != 80) {
        oss << ":" << req.port;
    }
    oss << "\r\n";

    // Other headers (skip hop-by-hop headers)
    for (const auto& [name, value] : req.headers) {
        if (name != "host" &&
            name != "connection" &&
            name != "keep-alive" &&
            name != "proxy-connection" &&
            name != "transfer-encoding" &&
            name != "upgrade") {
            oss << name << ": " << value << "\r\n";
        }
    }

    // Add Connection: close for simplicity
    oss << "Connection: close\r\n";

    // End of headers
    oss << "\r\n";

    return oss.str();
}

} // namespace proxy
