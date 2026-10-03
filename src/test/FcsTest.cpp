#include "Fcs.h"

#include <gtest/gtest.h>
#include <vector>

TEST(FcsTest, MatchesStoredTrailer)
{
    std::vector<uint8_t> frame(24, 0);
    frame[0] = 0x08;
    frame[1] = 0x01;
    uint8_t fcs[4];
    winject::wifi_fcs_store(frame.data(), frame.size(), fcs);
    frame.insert(frame.end(), fcs, fcs + 4);
    EXPECT_TRUE(winject::wifi_fcs_matches(frame.data(), frame.size()));
    frame[27] ^= 0xff;
    EXPECT_FALSE(winject::wifi_fcs_matches(frame.data(), frame.size()));
}
