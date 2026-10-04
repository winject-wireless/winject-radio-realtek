#include "Config.h"

#include <gtest/gtest.h>
#include <fstream>

TEST(ConfigTest, RejectsUnknownKey)
{
    const std::string path = "/tmp/winject-radio-cfg-test.cfg";
    std::ofstream out(path);
    out << "state.dir = /tmp\n";
    out << "radio.device = wlx0\n";
    out << "bogus = 1\n";
    out.close();
    winject::AppConfig cfg;
    std::string err;
    EXPECT_FALSE(cfg.load(path, &err));
}

TEST(ConfigTest, RequiresOneSelector)
{
    const std::string path = "/tmp/winject-radio-cfg-test2.cfg";
    std::ofstream out(path);
    out << "state.dir = /tmp\n";
    out.close();
    winject::AppConfig cfg;
    std::string err;
    EXPECT_FALSE(cfg.load(path, &err));
}

TEST(ConfigTest, Defaults)
{
    const std::string path =
        std::string(SOURCE_DIR) + "/configuration/radio-a.cfg";
    winject::AppConfig cfg;
    std::string err;
    ASSERT_TRUE(cfg.load(path, &err)) << err;
    EXPECT_EQ(cfg.net.console_port, 2201u);
    EXPECT_EQ(cfg.net.dplane_port, 9000u);
    EXPECT_EQ(cfg.tune.tx_queue_sz, 20u);
    EXPECT_EQ(cfg.radio.device, "wlx00c0cabce06f");
    EXPECT_EQ(cfg.radio.txpower,
              std::string(SOURCE_DIR) + "/configuration/txpower.csv");
}

TEST(ConfigTest, RequiresTxpower)
{
    const std::string path = ::testing::TempDir() + "/no_txpower.cfg";
    {
        std::ofstream out(path);
        out << "radio.device = wlan0\nstate.dir = /tmp/x\n";
    }
    winject::AppConfig cfg;
    std::string err;
    EXPECT_FALSE(cfg.load(path, &err));
    EXPECT_NE(err.find("radio.txpower"), std::string::npos) << err;
}

TEST(ConfigTest, RejectsLegacyDplanePorts)
{
    const std::string path = ::testing::TempDir() + "/legacy_dplane.cfg";
    for (const char* line :
         {"net.inject_port = 9000", "net.forward_port = 9210"})
    {
        {
            std::ofstream out(path);
            out << "radio.device = wlan0\nstate.dir = /tmp/x\nradio.txpower = t.csv\n"
                << line << "\n";
        }
        winject::AppConfig cfg;
        std::string err;
        EXPECT_FALSE(cfg.load(path, &err)) << line;
        EXPECT_NE(err.find("net.dplane_port"), std::string::npos) << err;
    }
}

TEST(ConfigTest, LdpcAndStbcKeysRejected)
{
    for (const char* line : {"radio.ldpc = false", "radio.stbc = 0"})
    {
        const std::string path = ::testing::TempDir() + "/ldpc_stbc.cfg";
        {
            std::ofstream out(path);
            out << "radio.device = wlan0\nstate.dir = /tmp/x\nradio.txpower = t.csv\n" << line << "\n";
        }
        winject::AppConfig cfg;
        std::string err;
        EXPECT_FALSE(cfg.load(path, &err)) << line;
        EXPECT_NE(err.find("unknown key"), std::string::npos) << err;
    }
}
