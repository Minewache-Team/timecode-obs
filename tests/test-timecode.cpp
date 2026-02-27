/*
 * test-timecode.cpp - Unit tests for SMPTE timecode generation
 */

#include <gtest/gtest.h>

extern "C" {
#include "timecode.h"
}

TEST(TimecodeTest, BasicConversion30fps)
{
	smpte_timecode_t tc;
	/* 2024-01-01 14:30:22.000 UTC = Unix 1704119422 */
	/* 14:30:22 time-of-day */
	int64_t unix_sec = 14 * 3600 + 30 * 60 + 22; /* simplified: just time-of-day */
	ASSERT_TRUE(timecode_from_unix(unix_sec, 0, TC_FPS_30, &tc));
	EXPECT_EQ(tc.hours, 14);
	EXPECT_EQ(tc.minutes, 30);
	EXPECT_EQ(tc.seconds, 22);
	EXPECT_EQ(tc.frames, 0);
	EXPECT_FALSE(tc.drop_frame);
}

TEST(TimecodeTest, FrameFromMicroseconds25fps)
{
	smpte_timecode_t tc;
	/* 500000 usec = 0.5s => frame 12 at 25fps */
	ASSERT_TRUE(timecode_from_unix(0, 500000, TC_FPS_25, &tc));
	EXPECT_EQ(tc.frames, 12);
}

TEST(TimecodeTest, FrameFromMicroseconds30fps)
{
	smpte_timecode_t tc;
	/* 500000 usec = 0.5s => frame 15 at 30fps */
	ASSERT_TRUE(timecode_from_unix(0, 500000, TC_FPS_30, &tc));
	EXPECT_EQ(tc.frames, 15);
}

TEST(TimecodeTest, MidnightRollover)
{
	smpte_timecode_t tc;
	/* 23:59:59 + 29/30 frame => should still be valid */
	int64_t unix_sec = 23 * 3600 + 59 * 60 + 59;
	ASSERT_TRUE(timecode_from_unix(unix_sec, 966666, TC_FPS_30, &tc));
	EXPECT_EQ(tc.hours, 23);
	EXPECT_EQ(tc.minutes, 59);
	EXPECT_EQ(tc.seconds, 59);
	EXPECT_EQ(tc.frames, 29);
}

TEST(TimecodeTest, DropFrame2997)
{
	smpte_timecode_t tc;
	/* At minute boundary (not divisible by 10), frames 0 and 1 should be skipped */
	/* 01:01:00.000 => should have frame=2 (not 0) because of drop-frame */
	int64_t unix_sec = 1 * 3600 + 1 * 60 + 0;
	ASSERT_TRUE(timecode_from_unix(unix_sec, 0, TC_FPS_29_97_DF, &tc));
	EXPECT_TRUE(tc.drop_frame);
	EXPECT_EQ(tc.frames, 2); /* frames 0,1 dropped at non-10th minutes */
}

TEST(TimecodeTest, DropFrameNoSkipAt10thMinute)
{
	smpte_timecode_t tc;
	/* At 10th minute boundary, no skip */
	int64_t unix_sec = 1 * 3600 + 10 * 60 + 0;
	ASSERT_TRUE(timecode_from_unix(unix_sec, 0, TC_FPS_29_97_DF, &tc));
	EXPECT_TRUE(tc.drop_frame);
	EXPECT_EQ(tc.frames, 0); /* no skip at 10th minute */
}

TEST(TimecodeTest, AllFramerates)
{
	smpte_timecode_t tc;
	EXPECT_TRUE(timecode_from_unix(0, 0, TC_FPS_24, &tc));
	EXPECT_TRUE(timecode_from_unix(0, 0, TC_FPS_25, &tc));
	EXPECT_TRUE(timecode_from_unix(0, 0, TC_FPS_29_97_DF, &tc));
	EXPECT_TRUE(timecode_from_unix(0, 0, TC_FPS_30, &tc));
	EXPECT_TRUE(timecode_from_unix(0, 0, TC_FPS_50, &tc));
	EXPECT_TRUE(timecode_from_unix(0, 0, TC_FPS_60, &tc));
}

TEST(TimecodeTest, ToString)
{
	smpte_timecode_t tc;
	timecode_from_unix(14 * 3600 + 30 * 60 + 22, 500000, TC_FPS_30, &tc);

	char buf[16];
	timecode_to_string(&tc, buf, sizeof(buf));
	EXPECT_STREQ(buf, "14:30:22:15");
}

TEST(TimecodeTest, ToStringDropFrame)
{
	smpte_timecode_t tc;
	timecode_from_unix(14 * 3600 + 30 * 60 + 22, 500000, TC_FPS_29_97_DF, &tc);

	char buf[16];
	timecode_to_string(&tc, buf, sizeof(buf));
	/* Drop-frame uses semicolon separator */
	EXPECT_STREQ(buf, "14:30:22;15");
}

TEST(TimecodeTest, NullPointerSafety)
{
	EXPECT_FALSE(timecode_from_unix(0, 0, TC_FPS_30, nullptr));

	char buf[16];
	timecode_to_string(nullptr, buf, sizeof(buf));
}
