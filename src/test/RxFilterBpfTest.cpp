#include "RxFilterBpf.h"

#include <gtest/gtest.h>

TEST(RxFilterBpfTest, KeepsMatchingAddr3)
{
    mac_filter f;
    f.enabled = true;
    f.addr[0] = 0xca;
    f.addr[1] = 0xfe;
    f.addr[2] = 0xba;
    f.addr[3] = 0xbe;
    f.addr[4] = 0x04;
    f.addr[5] = 0xd2;
    const auto prog = winject::rx_filter_bpf_program(f);
    ASSERT_FALSE(prog.empty());
    uint8_t frame[64] = {};
    frame[2] = 24;
    frame[3] = 0;
    const size_t mpdu_off = 24;
    frame[mpdu_off + 16] = 0xca;
    frame[mpdu_off + 17] = 0xfe;
    frame[mpdu_off + 18] = 0xba;
    frame[mpdu_off + 19] = 0xbe;
    frame[mpdu_off + 20] = 0x04;
    frame[mpdu_off + 21] = 0xd2;
    EXPECT_TRUE(winject::bpf_interpret(prog.data(), prog.size(), frame,
                                       sizeof(frame)));
}
