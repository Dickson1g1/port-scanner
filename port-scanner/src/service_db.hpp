#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_map>

// Maps port numbers to IANA-registered service names.
// Using string_view (not std::string) avoids heap allocation for literal strings —
// all values are stored in the read-only data segment.
inline const std::unordered_map<uint16_t, std::string_view> kServiceNames = {
    {20,   "ftp-data"},
    {21,   "ftp"},
    {22,   "ssh"},
    {23,   "telnet"},
    {25,   "smtp"},
    {53,   "dns"},
    {67,   "dhcp"},
    {68,   "dhcp-client"},
    {69,   "tftp"},
    {80,   "http"},
    {110,  "pop3"},
    {111,  "rpcbind"},
    {119,  "nntp"},
    {123,  "ntp"},
    {135,  "msrpc"},
    {139,  "netbios-ssn"},
    {143,  "imap"},
    {161,  "snmp"},
    {194,  "irc"},
    {389,  "ldap"},
    {443,  "https"},
    {445,  "smb"},
    {465,  "smtps"},
    {514,  "syslog"},
    {587,  "submission"},
    {631,  "ipp"},
    {636,  "ldaps"},
    {993,  "imaps"},
    {995,  "pop3s"},
    {1433, "mssql"},
    {1521, "oracle"},
    {2375, "docker"},
    {2376, "docker-tls"},
    {3000, "dev-server"},
    {3306, "mysql"},
    {3389, "rdp"},
    {4443, "alt-https"},
    {5432, "postgresql"},
    {5900, "vnc"},
    {6379, "redis"},
    {6443, "k8s-api"},
    {8080, "http-alt"},
    {8443, "https-alt"},
    {8888, "jupyter"},
    {9200, "elasticsearch"},
    {9300, "elasticsearch-cluster"},
    {27017,"mongodb"},
    {27018,"mongodb-shard"},
};

// Returns the service name for a port, or "unknown" if not in the table.
inline std::string_view service_name(uint16_t port) {
    auto it = kServiceNames.find(port);
    return it != kServiceNames.end() ? it->second : "unknown";
}
