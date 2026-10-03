#ifndef WINJECT_RADIO_CONFIG_H_
#define WINJECT_RADIO_CONFIG_H_

#include "Log.h"

#include <cstdint>
#include <string>

namespace winject
{

struct RadioConfig
{
    std::string device;
    std::string usb_port;
    std::string mac;
    std::string driver = "rtl88xxau_wfb";
    std::string regdom = "BO";
    unsigned bandwidth = 20;
    std::string txpower;  // txpower.csv; relative paths resolve against the config file
    bool rx_bpf = false;
    unsigned tx_retry_us = 50000;
};

struct NetConfig
{
    std::string bind_addr = "0.0.0.0";
    uint16_t console_port = 2201;
    uint16_t inject_port = 9000;
    uint16_t forward_port = 9210;
    std::string trusted_ipv4;
};

struct TuneConfig
{
    unsigned tx_queue_sz = 20;
    unsigned rx_batch = 16;
    unsigned sock_rcvbuf = 4194304;
};

struct AppConfig
{
    RadioConfig radio;
    NetConfig net;
    std::string state_dir;
    TuneConfig tune;
    LogLevel log_level = LogLevel::info;

    bool load(const std::string& path, std::string* error);
};

}  // namespace winject

#endif  // WINJECT_RADIO_CONFIG_H_
