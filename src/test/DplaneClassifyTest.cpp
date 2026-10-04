#include "DplaneClassify.h"

#include <gtest/gtest.h>

TEST(DplaneClassifyTest, LengthTable)
{
    EXPECT_EQ(winject::dplane_classify(0, false), winject::DplaneKind::invalid);
    EXPECT_EQ(winject::dplane_classify(1, false),
              winject::DplaneKind::registration);
    EXPECT_EQ(winject::dplane_classify(23, false),
              winject::DplaneKind::registration);
    EXPECT_EQ(winject::dplane_classify(24, false), winject::DplaneKind::mpdu);
    EXPECT_EQ(winject::dplane_classify(1472, false), winject::DplaneKind::mpdu);
    EXPECT_EQ(winject::dplane_classify(1473, false), winject::DplaneKind::invalid);
    EXPECT_EQ(winject::dplane_classify(24, true), winject::DplaneKind::invalid);
}
