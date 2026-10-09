#include "DriverTune.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{

std::string make_params(const std::string& name, bool with_params)
{
    const std::string root = ::testing::TempDir() + "/" + name;
    std::filesystem::remove_all(root);
    const std::string dir = root + "/drv/module/parameters";
    std::filesystem::create_directories(dir);
    if (with_params)
    {
        std::ofstream(dir + "/rtw_monitor_igi") << "0\n";
        std::ofstream(dir + "/rtw_monitor_cck_pd") << "0\n";
    }
    return root;
}

std::string read(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

winject::RadioConfig radio(unsigned igi, unsigned pd)
{
    winject::RadioConfig r;
    r.driver = "drv";
    r.rx_igi = igi;
    r.rx_cck_pd = pd;
    return r;
}

}  // namespace

TEST(DriverTuneTest, WritesValues)
{
    const std::string root = make_params("dt_write", true);
    const auto res = winject::apply_rx_gain(radio(0x3c, 0x8a), root);
    EXPECT_TRUE(res.igi_applied);
    EXPECT_TRUE(res.cck_pd_applied);
    EXPECT_FALSE(res.params_missing);
    EXPECT_EQ(read(root + "/drv/module/parameters/rtw_monitor_igi"), "60\n");
    EXPECT_EQ(read(root + "/drv/module/parameters/rtw_monitor_cck_pd"), "138\n");
}

TEST(DriverTuneTest, ZeroWritesBackToAuto)
{
    const std::string root = make_params("dt_zero", true);
    winject::apply_rx_gain(radio(0x3c, 0x8a), root);
    const auto res = winject::apply_rx_gain(radio(0, 0), root);
    EXPECT_FALSE(res.igi_applied);
    EXPECT_FALSE(res.cck_pd_applied);
    EXPECT_EQ(read(root + "/drv/module/parameters/rtw_monitor_igi"), "0\n");
    EXPECT_EQ(read(root + "/drv/module/parameters/rtw_monitor_cck_pd"), "0\n");
}

TEST(DriverTuneTest, MissingParamsWarnOnlyWhenRequested)
{
    const std::string root = make_params("dt_missing", false);
    EXPECT_FALSE(winject::apply_rx_gain(radio(0, 0), root).params_missing);
    const auto res = winject::apply_rx_gain(radio(0x3c, 0), root);
    EXPECT_TRUE(res.params_missing);
    EXPECT_FALSE(res.igi_applied);
}
