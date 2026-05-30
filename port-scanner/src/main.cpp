#include "scanner.hpp"
#include "service_db.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstdlib>
#include <chrono>

// ANSI escape codes for terminal colors.
// We use these directly rather than a library to keep dependencies minimal.
// Each code resets to default at the end of the line with RESET.
namespace color {
    constexpr auto RESET   = "\033[0m";
    constexpr auto RED     = "\033[31m";
    constexpr auto GREEN   = "\033[32m";
    constexpr auto YELLOW  = "\033[33m";
    constexpr auto CYAN    = "\033[36m";
    constexpr auto BOLD    = "\033[1m";
    constexpr auto DIM     = "\033[2m";
}

// Print a single result line as it arrives (called from the callback).
// Output format mirrors nmap for familiarity:
//   80/tcp   open    http     (12 ms)
static void print_result(const PortResult& r, bool show_closed) {
    if (r.status == PortStatus::CLOSED && !show_closed) return;

    std::ostringstream line;
    // Port/protocol column — right-aligned in 6 chars
    line << std::setw(6) << r.port << "/tcp   ";

    switch (r.status) {
        case PortStatus::OPEN:
            line << color::GREEN << color::BOLD << "open    " << color::RESET;
            line << color::CYAN << std::left << std::setw(20)
                 << service_name(r.port) << color::RESET;
            line << color::DIM << "(" << r.latency_ms << " ms)" << color::RESET;
            break;
        case PortStatus::CLOSED:
            line << color::RED << "closed  " << color::RESET;
            line << color::DIM << service_name(r.port) << color::RESET;
            break;
        case PortStatus::FILTERED:
            line << color::YELLOW << "filtered" << color::RESET;
            break;
    }

    // Use \r\n to overwrite any progress output on the same line cleanly.
    std::cout << line.str() << "\n";
}

static void print_usage(const char* prog) {
    std::cerr
        << color::BOLD << "Usage: " << color::RESET
        << prog << " [options] <host>\n\n"
        << "Options:\n"
        << "  -p <start>-<end>   Port range  (default: 1-1024)\n"
        << "  -p <port>          Single port\n"
        << "  -c <n>             Concurrency (default: 256)\n"
        << "  -t <ms>            Timeout ms  (default: 2000)\n"
        << "  -v                 Show closed ports\n"
        << "  -h                 Show this help\n\n"
        << "Examples:\n"
        << "  " << prog << " scanme.nmap.org\n"
        << "  " << prog << " -p 1-65535 -c 512 -t 1000 192.168.1.1\n"
        << "  " << prog << " -p 80 example.com\n";
}

int main(int argc, char* argv[]) {
    ScanConfig config;
    bool show_closed = false;

    // Simple manual argument parsing — avoids getopt's non-portable details.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v") {
            show_closed = true;
        } else if (arg == "-p" && i + 1 < argc) {
            std::string range = argv[++i];
            auto dash = range.find('-');
            if (dash == std::string::npos) {
                // Single port
                config.port_start = config.port_end =
                    static_cast<uint16_t>(std::stoi(range));
            } else {
                config.port_start =
                    static_cast<uint16_t>(std::stoi(range.substr(0, dash)));
                config.port_end   =
                    static_cast<uint16_t>(std::stoi(range.substr(dash + 1)));
            }
        } else if (arg == "-c" && i + 1 < argc) {
            config.concurrency = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "-t" && i + 1 < argc) {
            config.timeout_ms = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg[0] != '-') {
            // Non-flag argument is the target host
            config.host = arg;
        }
    }

    if (config.host.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    if (config.port_start > config.port_end) {
        std::cerr << "Error: start port must be <= end port\n";
        return 1;
    }

    uint32_t total = static_cast<uint32_t>(config.port_end - config.port_start + 1);

    // Print scan header
    std::cout << color::BOLD << "\nScanning " << color::CYAN
              << config.host << color::RESET << color::BOLD
              << " — ports " << config.port_start << "-" << config.port_end
              << " (" << total << " ports)"
              << "  concurrency=" << config.concurrency
              << "  timeout=" << config.timeout_ms << "ms"
              << color::RESET << "\n";
    std::cout << std::string(60, '-') << "\n";

    auto t_start = std::chrono::steady_clock::now();

    Scanner scanner(config);

    // Register the live-print callback.
    // Results arrive in completion order (not port order) for maximum speed.
    // Sorted output is available in scanner.run()'s return value.
    scanner.on_result([&show_closed](const PortResult& r) {
        print_result(r, show_closed);
    });

    auto results = scanner.run();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start
    ).count();

    // Print summary
    uint32_t open_count     = 0;
    uint32_t filtered_count = 0;
    for (const auto& r : results) {
        if (r.status == PortStatus::OPEN)     ++open_count;
        if (r.status == PortStatus::FILTERED) ++filtered_count;
    }

    std::cout << std::string(60, '-') << "\n"
              << color::BOLD << "Done in " << elapsed_ms << " ms  "
              << color::GREEN << open_count << " open  "
              << color::RESET << color::BOLD
              << color::YELLOW << filtered_count << " filtered  "
              << color::RESET << color::BOLD
              << (total - open_count - filtered_count) << " closed"
              << color::RESET << "\n\n";

    // Exit code: 0 if any ports are open, 1 if none — useful in scripts.
    return open_count > 0 ? 0 : 1;
}
