#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

// PortStatus describes the outcome of a single connection attempt.
// OPEN     = TCP handshake succeeded (SYN + SYN-ACK received)
// CLOSED   = Target actively refused the connection (RST received)
// FILTERED = No response within the timeout (firewall drop or unreachable)
enum class PortStatus { OPEN, CLOSED, FILTERED };

// One result per scanned port.
struct PortResult {
    uint16_t   port;
    PortStatus status;
    long       latency_ms;   // round-trip time for OPEN ports, 0 otherwise
};

// Callback type — called once per port as results arrive (from any thread).
// The callback is invoked with the lock held, so it must be fast.
using ResultCallback = std::function<void(const PortResult&)>;

// Scanner configuration — all fields have sensible defaults.
struct ScanConfig {
    std::string host;
    uint16_t    port_start    = 1;
    uint16_t    port_end      = 1024;
    uint16_t    concurrency   = 256;   // max simultaneous async connections
    uint32_t    timeout_ms    = 2000;  // per-port connect timeout in ms
    bool        show_closed   = false; // print closed ports (verbose mode)
};

class Scanner {
public:
    explicit Scanner(ScanConfig config);

    // Run the full scan synchronously. Blocks until all ports are done.
    // Returns all results sorted by port number.
    std::vector<PortResult> run();

    // Register a callback to receive results as they arrive.
    // Called from internal threads — must be thread-safe.
    void on_result(ResultCallback cb);

private:
    // Launches an async connect attempt for one port.
    void scan_port(uint16_t port);

    // Called by Asio when a connection attempt completes or times out.
    void on_connect(uint16_t port,
                    const boost::system::error_code& ec,
                    std::chrono::steady_clock::time_point start_time);

    // Schedules the next batch of ports if the concurrency slot is now free.
    void schedule_next();

    ScanConfig                      config_;
    ResultCallback                  callback_;
    std::vector<PortResult>        results_;
    std::mutex                      results_mutex_;
    std::atomic<uint16_t>          next_port_;     // next port to dispatch
    std::atomic<uint16_t>          in_flight_;     // currently active connections
    std::atomic<uint32_t>          completed_;     // finished ports (for progress)
};
