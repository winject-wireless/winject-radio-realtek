#include "DeviceWatch.h"

#include "Log.h"

#include <gtest/gtest.h>

namespace winject
{
namespace
{

TEST(DeviceWatchStateTest, StaysAttachedWhileIndexMatches)
{
    DeviceWatchState st("wlx00", 5);
    EXPECT_EQ(st.check(5, {}, "rtl88xxau_wfb", 0), WatchAction::none);
    EXPECT_EQ(st.check(5, {}, "rtl88xxau_wfb", 5000), WatchAction::none);
}

TEST(DeviceWatchStateTest, UnplugStaysLost)
{
    DeviceWatchState st("wlx00", 5);
    ASSERT_EQ(st.check(0, {}, "rtl88xxau_wfb", 100), WatchAction::none);
    EXPECT_EQ(st.check(0, {}, "rtl88xxau_wfb", 2000), WatchAction::none);
}

TEST(DeviceWatchStateTest, ReplugDebouncesOneSecond)
{
    DeviceWatchState st("wlx00", 5);
    ASSERT_EQ(st.check(0, {}, "rtl88xxau_wfb", 0), WatchAction::none);
    const int64_t t = 1000;
    EXPECT_EQ(st.check(7, "rtl88xxau_wfb", "rtl88xxau_wfb", t), WatchAction::none);
    EXPECT_EQ(st.check(7, "rtl88xxau_wfb", "rtl88xxau_wfb", t + 999),
              WatchAction::none);
    EXPECT_EQ(st.check(7, "rtl88xxau_wfb", "rtl88xxau_wfb", t + 1000),
              WatchAction::restart);
}

TEST(DeviceWatchStateTest, CandidateIndexChangeRestartsDebounce)
{
    DeviceWatchState st("wlx00", 5);
    ASSERT_EQ(st.check(0, {}, "rtl88xxau_wfb", 0), WatchAction::none);
    const int64_t t = 100;
    ASSERT_EQ(st.check(7, "rtl88xxau_wfb", "rtl88xxau_wfb", t),
              WatchAction::none);
    EXPECT_EQ(st.check(8, "rtl88xxau_wfb", "rtl88xxau_wfb", t + 500),
              WatchAction::none);
    EXPECT_EQ(st.check(8, "rtl88xxau_wfb", "rtl88xxau_wfb", t + 1500),
              WatchAction::restart);
}

TEST(DeviceWatchStateTest, CandidateLostThenBackRestartsDebounce)
{
    DeviceWatchState st("wlx00", 5);
    ASSERT_EQ(st.check(0, {}, "rtl88xxau_wfb", 0), WatchAction::none);
    const int64_t t = 100;
    ASSERT_EQ(st.check(7, "rtl88xxau_wfb", "rtl88xxau_wfb", t),
              WatchAction::none);
    ASSERT_EQ(st.check(0, {}, "rtl88xxau_wfb", t + 500), WatchAction::none);
    ASSERT_EQ(st.check(7, "rtl88xxau_wfb", "rtl88xxau_wfb", t + 1000),
              WatchAction::none);
    EXPECT_EQ(st.check(7, "rtl88xxau_wfb", "rtl88xxau_wfb", t + 2000),
              WatchAction::restart);
}

TEST(DeviceWatchStateTest, WrongDriverNeverRestarts)
{
    DeviceWatchState st("wlx00", 5);
    ASSERT_EQ(st.check(0, {}, "rtl88xxau_wfb", 0), WatchAction::none);
    EXPECT_EQ(st.check(7, "88XXau", "rtl88xxau_wfb", 100), WatchAction::none);
    EXPECT_EQ(st.check(7, "88XXau", "rtl88xxau_wfb", 5000), WatchAction::none);
    EXPECT_EQ(st.check(7, "rtl88xxau_wfb", "rtl88xxau_wfb", 6000),
              WatchAction::none);
    EXPECT_EQ(st.check(7, "rtl88xxau_wfb", "rtl88xxau_wfb", 7000),
              WatchAction::restart);
}

TEST(DeviceWatchStateTest, MissedUnplugThenNewIndex)
{
    DeviceWatchState st("wlx00", 5);
    const int64_t t = 0;
    ASSERT_EQ(st.check(9, "rtl88xxau_wfb", "rtl88xxau_wfb", t),
              WatchAction::none);
    EXPECT_EQ(st.check(9, "rtl88xxau_wfb", "rtl88xxau_wfb", t + 1000),
              WatchAction::restart);
}

TEST(StartupWaitStateTest, TwoOkOneSecondApart)
{
    StartupWaitState st;
    EXPECT_FALSE(st.check(true, "wlx", 3, 0));
    EXPECT_TRUE(st.check(true, "wlx", 3, 1000));
}

TEST(StartupWaitStateTest, OkTooSoon)
{
    StartupWaitState st;
    EXPECT_FALSE(st.check(true, "wlx", 3, 0));
    EXPECT_FALSE(st.check(true, "wlx", 3, 500));
}

TEST(StartupWaitStateTest, OkInterruptedResetsAnchor)
{
    StartupWaitState st;
    EXPECT_FALSE(st.check(true, "wlx", 3, 0));
    EXPECT_FALSE(st.check(false, {}, -1, 500));
    EXPECT_FALSE(st.check(true, "wlx", 3, 1000));
    EXPECT_TRUE(st.check(true, "wlx", 3, 2000));
}

TEST(StartupWaitStateTest, IndexChangeRestartsDebounce)
{
    StartupWaitState st;
    EXPECT_FALSE(st.check(true, "wlx", 5, 0));
    EXPECT_FALSE(st.check(true, "wlx", 7, 1000));
    EXPECT_TRUE(st.check(true, "wlx", 7, 2000));
}

TEST(StartupWaitStateTest, NeverOk)
{
    StartupWaitState st;
    EXPECT_FALSE(st.check(false, {}, -1, 0));
    EXPECT_FALSE(st.check(false, {}, -1, 5000));
}

}  // namespace
}  // namespace winject
