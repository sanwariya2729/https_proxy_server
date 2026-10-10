cmake_minimum_required(VERSION 3.20)
project(secure_http_proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Main proxy executable
add_executable(proxy
    src/main.cpp
    src/http_parser.cpp
    src/policy_engine.cpp
    src/proxy_server.cpp
    src/metrics.cpp
)

target_include_directories(proxy PRIVATE include)

target_compile_options(proxy PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
)

target_link_libraries(proxy PRIVATE pthread)

# Test executables
add_executable(parser_tests
    tests/parser_tests.cpp
    src/http_parser.cpp
)

target_include_directories(parser_tests PRIVATE include)
target_compile_options(parser_tests PRIVATE -Wall -Wextra -Wpedantic)

add_executable(policy_tests
    tests/policy_tests.cpp
    src/policy_engine.cpp
)

target_include_directories(policy_tests PRIVATE include)
target_compile_options(policy_tests PRIVATE -Wall -Wextra -Wpedantic)

add_executable(integration_tests
    tests/integration_tests.cpp
    src/http_parser.cpp
    src/policy_engine.cpp
    src/proxy_server.cpp
    src/metrics.cpp
)

target_include_directories(integration_tests PRIVATE include)
target_compile_options(integration_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(integration_tests PRIVATE pthread)
