```
 ██████╗  ██████╗ ██████╗ ████████╗
 ██╔══██╗██╔═══██╗██╔══██╗╚══██╔══╝
 ██████╔╝██║   ██║██████╔╝   ██║   
 ██╔═══╝ ██║   ██║██╔══██╗   ██║   
 ██║     ╚██████╔╝██║  ██║   ██║   
 ╚═╝      ╚═════╝ ╚═╝  ╚═╝   ╚═╝   

 ███████╗ ██████╗ █████╗ ███╗   ██╗███╗   ██╗███████╗██████╗ 
 ██╔════╝██╔════╝██╔══██╗████╗  ██║████╗  ██║██╔════╝██╔══██╗
 ███████╗██║     ███████║██╔██╗ ██║██╔██╗ ██║█████╗  ██████╔╝
 ╚════██║██║     ██╔══██║██║╚██╗██║██║╚██╗██║██╔══╝  ██╔══██╗
 ███████║╚██████╗██║  ██║██║ ╚████║██║ ╚████║███████╗██║  ██║
 ╚══════╝ ╚═════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝

   async tcp · boost.asio · open · closed · filtered
```

# port-scanner

> An asynchronous TCP port scanner written in C++ using Boost.Asio.
> Scans thousands of ports concurrently without spawning thousands of threads —
> configurable port ranges, concurrency, timeouts, and color-coded output.

---

## What it does

`port-scanner` connects to a target host and attempts a TCP handshake on each
requested port. Rather than blocking on each connection, it uses Boost.Asio's
async I/O model to keep hundreds of attempts in-flight simultaneously. Each
port is reported as `open`, `closed`, or `filtered` as results arrive in
real time.

```
Scanning scanme.nmap.org — ports 1-1024 (1024 ports)  concurrency=256  timeout=2000ms
------------------------------------------------------------
    22/tcp   open    ssh                  (18 ms)
    80/tcp   open    http                 (21 ms)
   443/tcp   open    https                (19 ms)
   111/tcp   filtered
------------------------------------------------------------
Done in 4312 ms  3 open  1 filtered  1020 closed
```

---

## Features

- **Async TCP scanning** via Boost.Asio — hundreds of concurrent connection
  attempts multiplexed across a small thread pool, no one-thread-per-port overhead
- **Three port states** — `open` (TCP handshake succeeded), `closed` (RST
  received), `filtered` (no response within timeout — firewall drop)
- **Configurable port ranges** — single port, custom range, or full 1–65535 sweep
- **Adjustable concurrency** — tune simultaneous connections to balance speed
  against network load
- **Per-port timeout** — configurable deadline timer per connect; filtered ports
  are detected cleanly without blocking the event loop
- **Service detection** — maps 50+ well-known ports to their IANA service names
  (ssh, http, mysql, redis, postgresql, etc.)
- **Real-time output** — results are printed as they arrive, not batched at the end
- **ANSI color output** — green for open, red for closed, yellow for filtered
- **Shell-friendly exit codes** — `0` if any open ports found, `1` if none
- **Verbose mode** — `-v` flag shows closed ports alongside open and filtered

---

## Requirements

- Linux (tested on Ubuntu 22.04+, Fedora 38+, Arch)
- GCC 7+ or Clang 6+ (C++17 required)
- CMake 3.14+
- Boost 1.66+ (headers only — Asio is header-only)
- pthreads (standard on all Linux distros)

```bash
# Ubuntu / Debian
sudo apt install build-essential cmake libboost-dev

# Fedora / RHEL
sudo dnf install gcc-c++ cmake boost-devel

# Arch
sudo pacman -S base-devel cmake boost
```

---

## Build

```bash
git clone https://github.com/Dickson1g1/port-scanner.git
cd port-scanner

# Configure (Release build — optimised)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile on all CPU cores
cmake --build build -j$(nproc)

# Binary is at:
./build/pscanner
```

---

## Usage

```bash
pscanner [options] 

Options:
  -p -   Port range  (default: 1-1024)
  -p           Single port
  -c              Concurrency (default: 256)
  -t             Timeout ms  (default: 2000)
  -v                 Show closed ports (verbose)
  -h                 Show help
```

### Examples

```bash
# Default scan — ports 1-1024
./build/pscanner scanme.nmap.org

# Full port sweep with higher concurrency and shorter timeout
./build/pscanner -p 1-65535 -c 512 -t 1000 scanme.nmap.org

# Single port check
./build/pscanner -p 443 example.com

# Scan a local network device
./build/pscanner -p 1-1024 192.168.1.1

# Verbose — include closed ports in output
./build/pscanner -v -p 20-25 scanme.nmap.org

# Use in a shell script — exit code 0 means port is open
./build/pscanner -p 22 192.168.1.1 && echo "SSH is up"
```

---

## Exit codes

| Code | Meaning |
|------|---------|
| `0`  | One or more open ports found |
| `1`  | No open ports found, or scan failed |

---

## Port states

| State      | Meaning |
|------------|---------|
| `open`     | TCP handshake completed — a service is listening |
| `closed`   | Target sent RST — nothing listening on this port |
| `filtered` | No response within timeout — likely a firewall drop |

---

## How it works

Traditional scanners spawn one OS thread per port — at 1024 simultaneous
scans that means 1024 threads, each blocked waiting for a kernel response.
This scanner uses a single Boost.Asio `io_context` (event loop) shared
across a small thread pool sized to `hardware_concurrency()`.

Each port attempt is a non-blocking `async_connect` call paired with a
`steady_timer` for timeout. When the kernel reports a result — connection
established, reset received, or timer expired — Asio calls the completion
handler from one of the pool threads. The handler records the result,
dispatches the next pending port, and returns in microseconds.

This means 512 concurrent scans require 512 kernel TCP state machines but
only 4–8 OS threads, dramatically reducing memory and context-switch overhead.

---

## Project structure

```
port-scanner/
├── CMakeLists.txt
├── src/
│   ├── main.cpp          # argument parsing, output, entry point
│   ├── scanner.hpp       # Scanner class + PortResult + ScanConfig
│   ├── scanner.cpp       # Boost.Asio async scan implementation
│   └── service_db.hpp    # well-known port → service name map
└── README.md
```

---

## Legal notice

Only scan hosts you own or have explicit written permission to scan.
Unauthorized port scanning may violate laws and terms of service in your
jurisdiction. `scanme.nmap.org` is a legal public test target maintained
by the nmap project for scanner testing.

---

## License

MIT — do whatever you want, attribution appreciated.
