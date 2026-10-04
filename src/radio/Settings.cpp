#include "Settings.h"

#include "mplane_backend.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace winject
{

namespace
{

bool ensure_dir(const std::string& path)
{
    struct stat st = {};
    if (stat(path.c_str(), &st) == 0)
    {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path.c_str(), 0700) != 0)
    {
        return false;
    }
    return true;
}

std::string trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
    {
        return "";
    }
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace

Settings::Settings(std::string state_dir) : state_dir_(std::move(state_dir))
{
    unlink((state_dir_ + "/reset_id").c_str());
}

std::string Settings::slot_path(uint8_t slot) const
{
    return state_dir_ + "/slot" + std::to_string(slot);
}

std::string Settings::current_path() const
{
    return state_dir_ + "/current";
}

bool Settings::load_current(SlotData* out, std::string* error)
{
    if (!ensure_dir(state_dir_))
    {
        if (error != nullptr)
        {
            *error = "cannot create state dir";
        }
        return false;
    }
    std::ifstream in(current_path());
    if (!in)
    {
        return false;
    }
    unsigned slot = 0;
    in >> slot;
    if (slot >= SETTINGS_SLOT_COUNT)
    {
        return false;
    }
    current_slot_ = static_cast<uint8_t>(slot);
    return read_slot_file(static_cast<uint8_t>(slot), out);
}

bool Settings::write_slot_file(uint8_t slot, const SlotData& data)
{
    const std::string tmp = slot_path(slot) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (f == nullptr)
    {
        return false;
    }
    fprintf(f, "version=1\n");
    fprintf(f, "channel=%u\n", data.radio.channel);
    fprintf(f, "tx_power=%d\n", data.radio.tx_power_dbm);
    fprintf(f, "modulation=%s\n", data.radio.modulation);
    fprintf(f, "cca=%s\n", data.radio.cca_enabled ? "true" : "false");
    if (data.rx_filter.enabled)
    {
        fprintf(f, "rx_filter_addr3=%02x:%02x:%02x:%02x:%02x:%02x\n",
                data.rx_filter.addr[0], data.rx_filter.addr[1],
                data.rx_filter.addr[2], data.rx_filter.addr[3],
                data.rx_filter.addr[4], data.rx_filter.addr[5]);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return rename(tmp.c_str(), slot_path(slot).c_str()) == 0;
}

bool Settings::read_slot_file(uint8_t slot, SlotData* out)
{
    std::ifstream in(slot_path(slot));
    if (!in)
    {
        return false;
    }
    SlotData slot_data;
    slot_data.radio = radio_config{};
    std::string line;
    while (std::getline(in, line))
    {
        const auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "channel")
        {
            slot_data.radio.channel = static_cast<uint8_t>(std::stoi(val));
        }
        else if (key == "tx_power")
        {
            slot_data.radio.tx_power_dbm = static_cast<int8_t>(std::stoi(val));
        }
        else if (key == "modulation")
        {
            snprintf(slot_data.radio.modulation, sizeof(slot_data.radio.modulation),
                     "%s", val.c_str());
        }
        else if (key == "cca")
        {
            slot_data.radio.cca_enabled = (val == "true");
        }
        else if (key == "rx_filter_addr3")
        {
            unsigned b[6];
            if (sscanf(val.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2],
                       &b[3], &b[4], &b[5]) == 6)
            {
                slot_data.rx_filter.enabled = true;
                for (int i = 0; i < 6; ++i)
                {
                    slot_data.rx_filter.addr[i] = static_cast<uint8_t>(b[i]);
                }
            }
        }
    }
    *out = slot_data;
    return true;
}

mplane_status Settings::save_slot(uint8_t slot, const SlotData& data)
{
    if (slot >= SETTINGS_SLOT_COUNT)
    {
        return mplane_status::invalid;
    }
    if (!ensure_dir(state_dir_) || !write_slot_file(slot, data))
    {
        return mplane_status::io_error;
    }
    FILE* f = fopen(current_path().c_str(), "w");
    if (f == nullptr)
    {
        return mplane_status::io_error;
    }
    fprintf(f, "%u\n", slot);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    current_slot_ = slot;
    return mplane_status::ok;
}

mplane_status Settings::load_slot(uint8_t slot, SlotData* out)
{
    if (slot >= SETTINGS_SLOT_COUNT)
    {
        return mplane_status::invalid;
    }
    if (!read_slot_file(slot, out))
    {
        return mplane_status::not_found;
    }
    current_slot_ = slot;
    FILE* f = fopen(current_path().c_str(), "w");
    if (f != nullptr)
    {
        fprintf(f, "%u\n", slot);
        fclose(f);
    }
    return mplane_status::ok;
}

void Settings::set_current_slot(uint8_t slot)
{
    current_slot_ = slot;
}

std::optional<uint8_t> Settings::current_slot() const
{
    return current_slot_;
}

}  // namespace winject
