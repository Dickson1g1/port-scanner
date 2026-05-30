#include "scanner.hpp"
#include "service_db.hpp"

// BOOST_ASIO_NO_DEPRECATED suppresses warnings from older Asio APIs
#define BOOST_ASIO_NO_DEPRECATED
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace asio = boost::asio;
using tcp      = asio::ip::tcp;

// ---------------------------------------------------------------------------
// How Boost.Asio async scanning works
// ---------------------------------------------------------------------------
// Instead of spawning one thread per port (which would exhaust system limits
// at ~1024+ simultaneous threads), we use a single io_context (event loop)
// shared across a small thread pool. Each port attempt is a lightweight async
// operation — it posts a connect request to the kernel and returns immediately.
// When the kernel reports a result (connected, refused, or timed out), Asio
// calls our completion handler from one of the pool threads.
//
// The concurrency_ counter limits how many async connects are in-flight at once.
// When one completes, we immediately dispatch the next pending port.
// ---------------------------------------------------------------------------

Scanner::Scanner(ScanConfig config)
    : config_(std::move(config))
    , next_port_(config_.port_start)
    , in_flight_(0)
    , completed_(0)
{}

void Scanner::on_result(ResultCallback cb) {
    callback_ = std::move(cb);
}

std::vector<PortResult> Scanner::run() {
    // io_context is the Asio event loop — all async operations post to it.
    asio::io_context io;

    // Resolve the target hostname to an IP address once before scanning.
    // We do this synchronously so every async connect can reuse the endpoint.
    tcp::resolver resolver(io);
    tcp::resolver::results_type endpoints;
    try {
        // resolve() returns a list of endpoints; we use the first one.
        endpoints = resolver.resolve(config_.host, "0");
    } catch (const std::exception& e) {
        std::cerr << "DNS resolution failed for '"
                  << config_.host << "': " << e.what() << "\n";
        return {};
    }

    // Extract the raw IP address from the resolved endpoint.
    // We store it once and reuse it for every port — avoids repeated DNS calls.
    auto target_address = endpoints.begin()->endpoint().address();

    // Total ports to scan — used for completion detection.
    const uint32_t total_ports =
        static_cast<uint32_t>(config_.port_end - config_.port_start + 1);

    // Launch the initial batch of async connects up to the concurrency limit.
    // Each scan_port() call fires one async_connect and returns immediately.
    uint16_t initial_batch = std::min(
        static_cast<uint32_t>(config_.concurrency),
        total_ports
    );
    for (uint16_t i = 0; i < initial_batch; ++i) {
        uint16_t port = next_port_.fetch_add(1);
        if (port > config_.port_end) break;
        scan_port_impl(io, target_address, port);
        ++in_flight_;
    }

    // Run the io_context on a thread pool.
    // Using hardware_concurrency() threads maximises kernel TCP parallelism.
    // The main thread also runs the event loop (hence +0 extra threads).
    unsigned hw = std::thread::hardware_concurrency();
    std::vector<std::thread> pool;
    pool.reserve(hw);
    for (unsigned i = 0; i < hw - 1; ++i) {
        pool.emplace_back([&io]{ io.run(); });
    }

    // Block the main thread in the event loop until all ports complete.
    io.run();

    // Join the thread pool — all work is done at this point.
    for (auto& t : pool) t.join();

    // Sort results by port number for clean output.
    std::sort(results_.begin(), results_.end(),
              [](const PortResult& a, const PortResult& b){
                  return a.port < b.port;
              });

    return results_;
}

// ---------------------------------------------------------------------------
// scan_port_impl: fires one async TCP connect with a timer for timeout
// ---------------------------------------------------------------------------
void Scanner::scan_port_impl(asio::io_context& io,
                              const asio::ip::address& addr,
                              uint16_t port) {
    // We allocate the socket and timer on the heap (via shared_ptr) so they
    // outlive this function call. The lambda captures them by value, keeping
    // them alive until the completion handler fires.
    auto socket = std::make_shared<tcp::socket>(io);
    auto timer  = std::make_shared<asio::steady_timer>(io);

    tcp::endpoint endpoint(addr, port);
    auto t_start = std::chrono::steady_clock::now();

    // Set a deadline timer. If the connect doesn't complete within timeout_ms,
    // we cancel the socket. The connect handler then sees operation_aborted
    // and records the port as FILTERED.
    timer->expires_after(std::chrono::milliseconds(config_.timeout_ms));
    timer->async_wait([socket](const boost::system::error_code& ec) {
        // ec is success when the timer fires normally (not cancelled).
        // Cancelling the socket will cause async_connect to complete
        // immediately with operation_aborted.
        if (!ec) {
            socket->cancel();
        }
    });

    // async_connect attempts a TCP handshake asynchronously.
    // The lambda runs when the kernel reports success or failure.
    socket->async_connect(endpoint,
        [this, socket, timer, port, t_start, &io]
        (const boost::system::error_code& ec)
    {
        // Cancel the timer so it doesn't fire after we're already done.
        timer->cancel();

        // Measure round-trip time for open ports.
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t_start
        ).count();

        PortResult result;
        result.port = port;
        result.latency_ms = 0;

        if (!ec) {
            // Success: TCP handshake completed — port is OPEN.
            result.status     = PortStatus::OPEN;
            result.latency_ms = elapsed;
            // Gracefully close the socket so the target doesn't log errors.
            boost::system::error_code close_ec;
            socket->shutdown(tcp::socket::shutdown_both, close_ec);
            socket->close(close_ec);
        } else if (ec == boost::asio::error::connection_refused) {
            // RST received — port is actively closed (service not listening).
            result.status = PortStatus::CLOSED;
        } else {
            // Timeout, unreachable, or cancelled — treat as filtered.
            // FILTERED means a firewall is silently dropping packets.
            result.status = PortStatus::FILTERED;
        }

        // Store result under lock (multiple threads call this concurrently).
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_.push_back(result);
            if (callback_) callback_(result);
        }

        // Decrement in-flight count and dispatch the next pending port.
        --in_flight_;
        ++completed_;

        uint16_t next = next_port_.fetch_add(1);
        if (next <= config_.port_end) {
            ++in_flight_;
            scan_port_impl(io, socket->remote_endpoint().address(), next);
        }
    });
}
