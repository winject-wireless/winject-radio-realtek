#include "Modulation.h"
#include "Radiotap.h"

#include <gtest/gtest.h>

TEST(ModulationTest, ShortPreambleRejected)
{
    EXPECT_TRUE(winject::modulation_short_preamble_rejected("CCK_11M_S"));
    EXPECT_NE(winject::modulation_find("CCK_11M_S")->reject, false);
}

TEST(ModulationTest, OfdmMcsTxHeaderLen)
{
    const winject::ModulationEntry* e =
        winject::modulation_find("OFDM_MCS7_LGI");
    ASSERT_NE(e, nullptr);
    winject::RadioOptions opt;
    winject::TxProfile p = winject::radiotap_build_tx(*e, opt, 1);
    EXPECT_EQ(p.len, 13u);
}

TEST(ModulationTest, Channel14DsssOnly)
{
    std::vector<uint8_t> ch;
    for (uint8_t i = 1; i <= 14; ++i)
    {
        ch.push_back(i);
    }
    EXPECT_TRUE(winject::modulation_valid_for_channel("DSS_1M_L", 14, ch, 20));
    EXPECT_FALSE(winject::modulation_valid_for_channel("OFDM_6M", 14, ch, 20));
}
