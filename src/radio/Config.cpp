#include "Config.h"

#include <bfc/configuration_parser.hpp>
#include <cctype>
#include <fstream>
#include <set>

namespace winject
{

namespace
{

std::string trim(const std::string& s)
{
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool parse_log_level(const std::string& text, LogLevel* out)
{
    if (text == "error")
    {
        *out = LogLevel::error;
        return true;
    }
    if (text == "warn")
    {
        *out = LogLevel::warn;
        return true;
    }
    if (text == "info")
    {
        *out = LogLevel::info;
        return true;
    }
    if (text == "debug")
    {
        *out = LogLevel::debug;
        return true;
    }
    return false;
}

}  // namespace

bool AppConfig::load(const std::string& path, std::string* error)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        *error = "cannot open " + path;
        return false;
    }

    static const std::set<std::string> k_known = {
        "radio.device",       "radio.usb_port",    "radio.mac",
        "radio.driver",       "radio.regdom",      "radio.bandwidth",
        "radio.txpower",      "radio.rx_bpf",      "radio.tx_retry_us",
        "net.bind",           "net.console_port",  "net.inject_port",
        "net.forward_port",   "net.trusted_ipv4",  "state.dir",
        "tune.tx_queue_sz",   "tune.rx_batch",     "tune.sock_rcvbuf",
        "log.level",
    };

    bfc::configuration_parser parser;
    std::string line;
    while (std::getline(in, line))
    {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#')
        {
            continue;
        }
        const auto eq = t.find('=');
        if (eq != std::string::npos)
        {
            const std::string key = trim(t.substr(0, eq));
            if (k_known.find(key) == k_known.end())
            {
                *error = "unknown key " + key;
                return false;
            }
        }
        parser.load_line(t);
    }

    auto dev = parser.arg("radio.device");
    auto port = parser.arg("radio.usb_port");
    auto mac = parser.arg("radio.mac");
    int selectors = (dev && !dev->empty()) + (port && !port->empty()) +
                    (mac && !mac->empty());
    if (selectors != 1)
    {
        *error = "set exactly one of radio.device, radio.usb_port, radio.mac";
        return false;
    }
    if (dev)
    {
        radio.device = *dev;
    }
    if (port)
    {
        radio.usb_port = *port;
    }
    if (mac)
    {
        radio.mac = *mac;
    }

    if (auto v = parser.arg("radio.driver"))
    {
        radio.driver = *v;
    }
    if (auto v = parser.arg("radio.regdom"))
    {
        if (v->size() != 2)
        {
            *error = "invalid radio.regdom";
            return false;
        }
        radio.regdom = *v;
    }
    if (auto bw = parser.as<unsigned>("radio.bandwidth"))
    {
        if (*bw != 20 && *bw != 40)
        {
            *error = "invalid radio.bandwidth";
            return false;
        }
        radio.bandwidth = *bw;
    }
    auto txpower = parser.arg("radio.txpower");
    if (!txpower || txpower->empty())
    {
        *error = "missing radio.txpower (path to txpower.csv)";
        return false;
    }
    radio.txpower = *txpower;
    if (radio.txpower[0] != '/')
    {
        const auto slash = path.rfind('/');
        if (slash != std::string::npos)
        {
            radio.txpower = path.substr(0, slash + 1) + radio.txpower;
        }
    }
    if (auto bpf = parser.as<bool>("radio.rx_bpf"))
    {
        radio.rx_bpf = *bpf;
    }
    if (auto retry = parser.as<unsigned>("radio.tx_retry_us"))
    {
        radio.tx_retry_us = *retry;
    }

    if (auto bind = parser.arg("net.bind"))
    {
        net.bind_addr = *bind;
    }
    if (auto p = parser.as<unsigned>("net.console_port"))
    {
        if (*p == 0 || *p > 65535)
        {
            *error = "invalid net.console_port";
            return false;
        }
        net.console_port = static_cast<uint16_t>(*p);
    }
    if (auto p = parser.as<unsigned>("net.inject_port"))
    {
        if (*p == 0 || *p > 65535)
        {
            *error = "invalid net.inject_port";
            return false;
        }
        net.inject_port = static_cast<uint16_t>(*p);
    }
    if (auto p = parser.as<unsigned>("net.forward_port"))
    {
        if (*p == 0 || *p > 65535)
        {
            *error = "invalid net.forward_port";
            return false;
        }
        net.forward_port = static_cast<uint16_t>(*p);
    }
    if (auto t = parser.arg("net.trusted_ipv4"))
    {
        net.trusted_ipv4 = *t;
    }

    if (!parser.arg("state.dir") || parser.arg("state.dir")->empty())
    {
        *error = "missing state.dir";
        return false;
    }
    state_dir = *parser.arg("state.dir");

    if (auto q = parser.as<unsigned>("tune.tx_queue_sz"))
    {
        if (*q < 1 || *q > 64)
        {
            *error = "invalid tune.tx_queue_sz";
            return false;
        }
        tune.tx_queue_sz = *q;
    }
    if (auto b = parser.as<unsigned>("tune.rx_batch"))
    {
        if (*b < 1 || *b > 64)
        {
            *error = "invalid tune.rx_batch";
            return false;
        }
        tune.rx_batch = *b;
    }
    if (auto r = parser.as<unsigned>("tune.sock_rcvbuf"))
    {
        tune.sock_rcvbuf = *r;
    }

    if (auto ll = parser.arg("log.level"))
    {
        if (!parse_log_level(*ll, &log_level))
        {
            *error = "invalid log.level";
            return false;
        }
    }

    return true;
}

}  // namespace winject
