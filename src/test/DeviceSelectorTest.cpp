#include "DeviceSelector.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(DeviceSelectorTest, UsbPortMissingNet)
{
    const fs::path root =
        fs::temp_directory_path() / "winject-radio-sysfs-usb";
    fs::create_directories(root / "bus/usb/devices/9-9.9");
    winject::RadioConfig rc;
    rc.usb_port = "9-9.9";
    rc.driver = "rtl88xxau_wfb";
    winject::DeviceSelector sel(root.string());
    winject::DeviceMatch match;
    std::string err;
    EXPECT_FALSE(sel.resolve(rc, &match, &err, nullptr));
    EXPECT_NE(err.find("no net"), std::string::npos);
}
