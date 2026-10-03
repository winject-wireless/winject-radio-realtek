#include "Modulation.h"
#include "Radiotap.h"

#include <cstring>
#include <gtest/gtest.h>

TEST(RadiotapTest, LegacyRateHeader)
{
    const winject::ModulationEntry* e = winject::modulation_find("OFDM_24M");
    ASSERT_NE(e, nullptr);
    winject::RadioOptions opt;
    winject::TxProfile p = winject::radiotap_build_tx(*e, opt, 1);
    EXPECT_EQ(p.len, 12u);
    EXPECT_EQ(p.bytes[8], 48);
}

TEST(RadiotapTest, ParseMinimalRx)
{
    uint8_t rt[] = {0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00};
    winject::RxRadiotapInfo info = winject::radiotap_parse_rx(rt, sizeof(rt));
    EXPECT_TRUE(info.ok);
    EXPECT_EQ(info.rt_len, 8u);
}

TEST(RadiotapTest, McsHeaderMatchesWfbNg)
{
    // wfb-ng radiotap_header_ht with MCS_HAVE_{MCS,BW,GI,STBC,FEC} = 0x37.
    const winject::ModulationEntry* e = winject::modulation_find("OFDM_MCS3_LGI");
    ASSERT_NE(e, nullptr);
    winject::RadioOptions opt;
    winject::TxProfile p = winject::radiotap_build_tx(*e, opt, 1);
    const uint8_t want[] = {0x00, 0x00, 0x0d, 0x00, 0x00, 0x80, 0x08,
                            0x00, 0x08, 0x00, 0x37, 0x00, 0x03};
    ASSERT_EQ(p.len, sizeof(want));
    EXPECT_EQ(0, memcmp(p.bytes, want, sizeof(want)));
}

TEST(RadiotapTest, McsFlagsBw40SgiNeverLdpcOrStbc)
{
    // Bits as rtw_monitor_xmit_entry reads them: BW_40 0x01, SGI 0x04.
    // FEC_LDPC (0x10) and STBC (0x60) must stay clear.
    const winject::ModulationEntry* e = winject::modulation_find("OFDM_MCS7_SGI");
    ASSERT_NE(e, nullptr);
    winject::RadioOptions opt;
    opt.bandwidth = 40;
    winject::TxProfile p = winject::radiotap_build_tx(*e, opt, 1);
    EXPECT_EQ(p.bytes[10], 0x37);
    EXPECT_EQ(p.bytes[11], 0x01 | 0x04);
    EXPECT_EQ(p.bytes[12], 7);
}
