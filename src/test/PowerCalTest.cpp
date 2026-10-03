#include "PowerCal.h"

#include <fstream>
#include <gtest/gtest.h>

namespace
{

winject::PowerCal shipped()
{
    winject::PowerCal cal;
    std::string err;
    EXPECT_TRUE(cal.load_csv(
        std::string(SOURCE_DIR) + "/configuration/txpower.csv", &err))
        << err;
    return cal;
}

std::string write_csv(const char* name, const char* text)
{
    const std::string path = ::testing::TempDir() + "/" + name;
    std::ofstream(path) << text;
    return path;
}

}  // namespace

TEST(PowerCalTest, ShippedCsvHasAllIndices)
{
    const winject::PowerCal cal = shipped();
    EXPECT_FALSE(cal.have[0]);
    for (int i = 1; i <= 63; ++i)
    {
        EXPECT_TRUE(cal.have[i]) << "idx " << i;
    }
    EXPECT_DOUBLE_EQ(cal.dbm[1], -11.5);
    EXPECT_DOUBLE_EQ(cal.dbm[63], 18.8);
}

TEST(PowerCalTest, PicksClosestMeasuredIndex)
{
    const winject::PowerCal cal = shipped();
    bool clamped = true;
    EXPECT_EQ(cal.dbm_to_idx(2, &clamped), 29);  // 1.50 dBm
    EXPECT_FALSE(clamped);
    EXPECT_EQ(cal.dbm_to_idx(10, &clamped), 42);  // 10.80, first of ties
    EXPECT_EQ(cal.dbm_to_idx(17, &clamped), 58);  // exact 17.00
    EXPECT_EQ(cal.dbm_to_idx(13, &clamped), 48);  // 12.90 vs 13.10: lower idx
}

TEST(PowerCalTest, NonMonotonicTablePicksNearest)
{
    // idx 21 = -0.60 is higher than idx 22-24 = -0.90.
    const winject::PowerCal cal = shipped();
    EXPECT_EQ(cal.dbm_to_idx(-1, nullptr), 22);
    EXPECT_EQ(cal.dbm_to_idx(0, nullptr), 25);  // 0.50 beats -0.60
}

TEST(PowerCalTest, NeverReturnsIndexZero)
{
    // idx 0 means "no override" (driver default, ~12.9 dBm).
    const winject::PowerCal cal = shipped();
    bool clamped = false;
    EXPECT_EQ(cal.dbm_to_idx(-20, &clamped), 1);
    EXPECT_TRUE(clamped);
}

TEST(PowerCalTest, ClampsAboveMeasuredRange)
{
    const winject::PowerCal cal = shipped();
    bool clamped = false;
    EXPECT_EQ(cal.dbm_to_idx(20, &clamped), 63);  // max 18.80 dBm
    EXPECT_TRUE(clamped);
}

TEST(PowerCalTest, CsvHeaderCommentsAndSparseRows)
{
    const std::string path = write_csv("sparse.csv",
                                       "idx,tx_power\n"
                                       "# comment\n"
                                       "\n"
                                       "0, 5.0\n"
                                       "10, 0.0\n"
                                       "40, 10.0\n");
    winject::PowerCal cal;
    std::string err;
    ASSERT_TRUE(cal.load_csv(path, &err)) << err;
    EXPECT_EQ(cal.dbm_to_idx(5, nullptr), 10);  // tie 0.0/10.0: lower idx
    EXPECT_EQ(cal.dbm_to_idx(9, nullptr), 40);
}

TEST(PowerCalTest, RejectsBadCsv)
{
    winject::PowerCal cal;
    std::string err;
    EXPECT_FALSE(cal.load_csv("/nonexistent/txpower.csv", &err));
    EXPECT_FALSE(cal.load_csv(write_csv("bad_idx.csv", "1,1\n64,2\n"), &err));
    EXPECT_FALSE(cal.load_csv(write_csv("bad_val.csv", "1,1\n2,x\n"), &err));
    EXPECT_FALSE(cal.load_csv(write_csv("dup.csv", "1,1\n1,2\n"), &err));
    EXPECT_FALSE(cal.load_csv(write_csv("one.csv", "0,9\n1,1\n"), &err));
}
