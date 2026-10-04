#ifndef WINJECT_RADIO_SETTINGS_H_
#define WINJECT_RADIO_SETTINGS_H_

#include "config_types.h"
#include "mplane_backend.h"

#include <cstdint>
#include <optional>
#include <string>

namespace winject
{

struct SlotData
{
    radio_config radio;
    mac_filter rx_filter;
};

class Settings
{
public:
    explicit Settings(std::string state_dir);

    bool load_current(SlotData* out, std::string* error);
    mplane_status save_slot(uint8_t slot, const SlotData& data);
    mplane_status load_slot(uint8_t slot, SlotData* out);
    void set_current_slot(uint8_t slot);
    std::optional<uint8_t> current_slot() const;

private:
    std::string state_dir_;
    std::optional<uint8_t> current_slot_;

    std::string slot_path(uint8_t slot) const;
    std::string current_path() const;
    bool write_slot_file(uint8_t slot, const SlotData& data);
    bool read_slot_file(uint8_t slot, SlotData* out);
};

}  // namespace winject

#endif  // WINJECT_RADIO_SETTINGS_H_
