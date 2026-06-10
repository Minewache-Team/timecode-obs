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
	/* 23:59:59 + frame 29/30: 29/30 = 0.9667s = 966667 usec */
	int64_t unix_sec = 23 * 3600 + 59 * 60 + 59;
	ASSERT_TRUE(timecode_from_unix(unix_sec, 966667, TC_FPS_30, &tc));
	EXPECT_EQ(tc.hours, 23);
	EXPECT_EQ(tc.minutes, 59);
	EXPECT_EQ(tc.seconds, 59);
	EXPECT_EQ(tc.frames, 29);
}

TEST(TimecodeTest, DropFrame2997)
{
	smpte_timecode_t tc;
	/* Wall clock 01:01:00.000. Standard SMPTE drop-frame: just before
	 * each minute mark the label lags the wall clock by ~2 frames (the
	 * skip of :00/:01 at the minute start re-aligns it). 3660 s elapsed
	 * = 109690 frames at 29.97 fps; renumbered into label space that is
	 * 01:00:59;28 — labels 01:01:00;00/;01 do not exist. */
	int64_t unix_sec = 1 * 3600 + 1 * 60 + 0;
	ASSERT_TRUE(timecode_from_unix(unix_sec, 0, TC_FPS_29_97_DF, &tc));
	EXPECT_TRUE(tc.drop_frame);
	EXPECT_EQ(tc.hours, 1);
	EXPECT_EQ(tc.minutes, 0);
	EXPECT_EQ(tc.seconds, 59);
	EXPECT_EQ(tc.frames, 28);
}

TEST(TimecodeTest, DropFrameNoSkipAt10thMinute)
{
	smpte_timecode_t tc;
	/* At 10-minute marks drop-frame and wall clock coincide (no labels
	 * dropped in minutes divisible by 10): 01:10:00.000 → 01:10:00;00. */
	int64_t unix_sec = 1 * 3600 + 10 * 60 + 0;
	ASSERT_TRUE(timecode_from_unix(unix_sec, 0, TC_FPS_29_97_DF, &tc));
	EXPECT_TRUE(tc.drop_frame);
	EXPECT_EQ(tc.hours, 1);
	EXPECT_EQ(tc.minutes, 10);
	EXPECT_EQ(tc.seconds, 0);
	EXPECT_EQ(tc.frames, 0);
}

TEST(TimecodeTest, DropFrameLabelsAreStrictlyMonotonic)
{
	/* Sample the middle of consecutive 29.97 fps frame periods across the
	 * 01:00:59 → 01:01:00 minute boundary (where labels are dropped) and
	 * require the label index to advance by exactly 1 per frame period.
	 * The old wall-clock-at-nominal-30 mapping emitted the label
	 * 01:01:00;02 three times in a row here — LTC decoders and NLEs see
	 * a stalled timecode and lose lock. */
	const int64_t k_start = 109640; /* ~3658.3 s after midnight */
	const int64_t k_end = 109740;   /* ~3661.7 s — spans the boundary */
	int64_t prev_idx = -1;

	for (int64_t k = k_start; k <= k_end; k++) {
		/* Start of frame k in µs is k*1001000000/30000; add half a
		 * frame period to sample mid-frame, away from boundaries. */
		int64_t t_us = k * 1001000000LL / 30000 + 16683;
		smpte_timecode_t tc;
		ASSERT_TRUE(timecode_from_unix(t_us / 1000000, t_us % 1000000,
					       TC_FPS_29_97_DF, &tc));
		int64_t idx = ((tc.hours * 60 + tc.minutes) * 60 + tc.seconds) *
				      30 + tc.frames;
		if (prev_idx >= 0) {
			/* In nominal index space a drop boundary advances by
			 * exactly 3 (labels ;00/;01 don't exist); everywhere
			 * else by exactly 1. Step 0 (the old duplicate-label
			 * bug) or step 2 (half-applied drop) must fail. */
			bool at_drop = (tc.seconds == 0 &&
					(tc.minutes % 10) != 0 &&
					tc.frames == 2 &&
					prev_idx % 30 == 29);
			int64_t expected_step = at_drop ? 3 : 1;
			EXPECT_EQ(idx, prev_idx + expected_step)
				<< "label discontinuity at frame period " << k
				<< " (" << (int)tc.hours << ":" << (int)tc.minutes
				<< ":" << (int)tc.seconds << ";" << (int)tc.frames
				<< ")";
		}
		/* Dropped labels must never appear: frames 0/1 are illegal at
		 * non-10th minute starts. */
		if (tc.seconds == 0 && (tc.minutes % 10) != 0)
			EXPECT_GE(tc.frames, 2);
		prev_idx = idx;
	}
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

TEST(TimecodeTest, DateExtraction)
{
	smpte_timecode_t tc;
	/* 2026-03-15 14:30:22 UTC = Unix 1773854222 */
	/* Precomputed: days since epoch for 2026-03-15 = 20527 */
	/* 20527 * 86400 = 1773532800, + 14*3600 + 30*60 + 22 = 1773585022 */
	int64_t unix_sec = 1773532800LL + 14 * 3600 + 30 * 60 + 22;
	ASSERT_TRUE(timecode_from_unix(unix_sec, 0, TC_FPS_25, &tc));
	EXPECT_EQ(tc.year, 26);
	EXPECT_EQ(tc.month, 3);
	EXPECT_EQ(tc.day, 15);
	EXPECT_EQ(tc.hours, 14);
	EXPECT_EQ(tc.minutes, 30);
	EXPECT_EQ(tc.seconds, 22);
}

TEST(TimecodeTest, DateEpoch)
{
	smpte_timecode_t tc;
	/* Unix epoch: 1970-01-01 */
	ASSERT_TRUE(timecode_from_unix(0, 0, TC_FPS_25, &tc));
	EXPECT_EQ(tc.year, 70);
	EXPECT_EQ(tc.month, 1);
	EXPECT_EQ(tc.day, 1);
}

TEST(TimecodeTest, DateLeapYear)
{
	smpte_timecode_t tc;
	/* 2024-02-29 (leap year) = days since epoch 19782 */
	/* 19782 * 86400 = 1709164800 */
	ASSERT_TRUE(timecode_from_unix(1709164800LL, 0, TC_FPS_25, &tc));
	EXPECT_EQ(tc.year, 24);
	EXPECT_EQ(tc.month, 2);
	EXPECT_EQ(tc.day, 29);
}
