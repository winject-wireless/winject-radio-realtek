#include "Settings.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;

TEST(SettingsTest, SaveLoadRoundTrip)
{
    const fs::path dir = fs::temp_directory_path() / "winject-radio-settings";
    fs::create_directories(dir);
    winject::Settings s(dir.string());
    winject::SlotData data;
    data.radio.channel = 6;
    data.radio.tx_power_dbm = 15;
    snprintf(data.radio.modulation, sizeof(data.radio.modulation), "%s",
             "OFDM_24M");
    data.radio.cca_enabled = true;
    data.rx_filter.enabled = true;
    data.rx_filter.addr[0] = 0xca;
    data.rx_filter.addr[5] = 0xd2;
    EXPECT_EQ(s.save_slot(0, data), mplane_status::ok);
    winject::SlotData loaded;
    EXPECT_EQ(s.load_slot(0, &loaded), mplane_status::ok);
    EXPECT_EQ(loaded.radio.channel, 6);
    EXPECT_TRUE(loaded.rx_filter.enabled);
}

TEST(SettingsTest, MissingSlot)
{
    const fs::path dir = fs::temp_directory_path() / "winject-radio-settings-miss";
    fs::create_directories(dir);
    winject::Settings s(dir.string());
    winject::SlotData loaded;
    EXPECT_EQ(s.load_slot(3, &loaded), mplane_status::not_found);
}
