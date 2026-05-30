cmake_minimum_required(VERSION 3.14)
project(port_scanner VERSION 1.0.0 LANGUAGES CXX)

# Require C++17 — we use std::optional, structured bindings, and if-init
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build-type defaults to Release (optimised) if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Extra warnings in Debug builds to catch issues early
set(CMAKE_CXX_FLAGS_DEBUG   "-g -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# Find Boost — we need the headers only (Asio is header-only)
find_package(Boost 1.66 REQUIRED)

add_executable(pscanner
  src/main.cpp
  src/scanner.cpp
)

# Link Boost headers and POSIX thread library (Asio needs it on Linux)
target_include_directories(pscanner PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(pscanner PRIVATE pthread)
