#include "../include/http_parser.hpp"
#include <iostream>
#include <cassert>

using namespace proxy;

void test_valid_get_request() {
    std::cout << "Test: Valid GET request... ";

    std::string request =
        "GET http://example.com/path HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: test\r\n"
        "\r\n";

    HttpParser parser;
    ParseError error;
    auto result = parser.parse(request, error);

    assert(result.has_value());
    assert(error == ParseError::None);
    assert(result->method == "GET");
    assert(result->host == "example.com");
    assert(result->port == 80);
    assert(result->path == "/path");

    std::cout << "PASS" << std::endl;
}

void test_malformed_request_line() {
    std::cout << "Test: Malformed request line... ";

    std::string request = "INVALID\r\n\r\n";

    HttpParser parser;
    ParseError error;
    auto result = parser.parse(request, error);

    assert(!result.has_value());
    assert(error == ParseError::MalformedRequestLine);

    std::cout << "PASS" << std::endl;
}

void test_missing_host() {
    std::cout << "Test: Missing Host header... ";

    std::string request =
        "GET /path HTTP/1.1\r\n"
        "User-Agent: test\r\n"
        "\r\n";

    HttpParser parser;
    ParseError error;
    auto result = parser.parse(request, error);

    assert(!result.has_value());
    assert(error == ParseError::MissingHost);

    std::cout << "PASS" << std::endl;
}

void test_header_too_large() {
    std::cout << "Test: Header too large... ";

    std::string request = "GET http://example.com/ HTTP/1.1\r\n";
    request += std::string(HttpParser::MAX_HEADER_SIZE, 'X');
    request += "\r\n\r\n";

    HttpParser parser;
    ParseError error;
    auto result = parser.parse(request, error);

    assert(!result.has_value());
    assert(error == ParseError::HeaderTooLarge);

    std::cout << "PASS" << std::endl;
}

void test_incomplete_headers() {
    std::cout << "Test: Incomplete headers... ";

    std::string request = "GET http://example.com/ HTTP/1.1\r\n";
    // No \r\n\r\n terminator

    HttpParser parser;
    ParseError error;
    auto result = parser.parse(request, error);

    assert(!result.has_value());
    assert(error == ParseError::Incomplete);

    std::cout << "PASS" << std::endl;
}

void test_request_with_port() {
    std::cout << "Test: Request with custom port... ";

    std::string request =
        "GET http://example.com:8080/path HTTP/1.1\r\n"
        "Host: example.com:8080\r\n"
        "\r\n";

    HttpParser parser;
    ParseError error;
    auto result = parser.parse(request, error);

    assert(result.has_value());
    assert(result->host == "example.com");
    assert(result->port == 8080);

    std::cout << "PASS" << std::endl;
}

void test_unsupported_method() {
    std::cout << "Test: Unsupported method... ";

    std::string request =
        "DELETE http://example.com/ HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "\r\n";

    HttpParser parser;
    ParseError error;
    auto result = parser.parse(request, error);

    assert(!result.has_value());
    assert(error == ParseError::UnsupportedMethod);

    std::cout << "PASS" << std::endl;
}

void test_case_insensitive_headers() {
    std::cout << "Test: Case-insensitive headers... ";

    std::string request =
        "GET http://example.com/ HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: test\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n";

    HttpParser parser;
    ParseError error;
    auto result = parser.parse(request, error);

    assert(result.has_value());
    assert(result->headers.find("user-agent") != result->headers.end());
    assert(result->headers.find("content-type") != result->headers.end());

    std::cout << "PASS" << std::endl;
}

int main() {
    std::cout << "=== HTTP Parser Tests ===" << std::endl;

    test_valid_get_request();
    test_malformed_request_line();
    test_missing_host();
    test_header_too_large();
    test_incomplete_headers();
    test_request_with_port();
    test_unsupported_method();
    test_case_insensitive_headers();

    std::cout << "\nAll parser tests passed!" << std::endl;
    return 0;
}
