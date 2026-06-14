/*
 * test-ntp-offset.cpp - Unit tests for NTP offset calculation
 *
 * Note: These tests verify the API surface and corrected-time logic
 * without actually hitting an NTP server (to avoid network dependency in CI).
 */

#include <gtest/gtest.h>

extern "C" {
#include "ntp-client.h"
}

TEST(NTPOffsetTest, PlatformInitCleanup)
{
	EXPECT_TRUE(ntp_platform_init());
	ntp_platform_cleanup();
}

TEST(NTPOffsetTest, CorrectedTimePositiveOffset)
{
	int64_t sec, usec;

	/* With zero offset, should return roughly current time */
	ntp_corrected_time(0, &sec, &usec);
	EXPECT_GT(sec, 0);
	EXPECT_GE(usec, 0);
	EXPECT_LT(usec, 1000000);
}

TEST(NTPOffsetTest, CorrectedTimeAppliesOffset)
{
	int64_t sec1, usec1, sec2, usec2;

	ntp_corrected_time(0, &sec1, &usec1);
	ntp_corrected_time(1000, &sec2, &usec2); /* +1 second offset */

	/* sec2 should be about 1 second ahead of sec1 */
	int64_t diff = sec2 - sec1;
	EXPECT_GE(diff, 0);
	EXPECT_LE(diff, 2); /* allow for timing */
}

TEST(NTPOffsetTest, CorrectedTimeNegativeOffset)
{
	int64_t sec1, usec1, sec2, usec2;

	ntp_corrected_time(0, &sec1, &usec1);
	ntp_corrected_time(-5000, &sec2, &usec2); /* -5 seconds offset */

	int64_t diff = sec1 - sec2;
	EXPECT_GE(diff, 4);
	EXPECT_LE(diff, 6);
}

TEST(NTPOffsetTest, QueryNullParams)
{
	ntp_result_t result;
	EXPECT_FALSE(ntp_query(nullptr, 2000, &result));
	EXPECT_FALSE(ntp_query("pool.ntp.org", 2000, nullptr));
}

/* ---- ntp_select_best_sample ---- */

static ntp_result_t make_sample(bool success, int64_t offset_ms, int64_t roundtrip_ms)
{
	ntp_result_t r = {};
	r.success = success;
	r.offset_ms = offset_ms;
	r.roundtrip_ms = roundtrip_ms;
	return r;
}

TEST(NTPSelectBestSample, PicksLowestRoundtrip)
{
	ntp_result_t s[3] = {
		make_sample(true, 100, 80),
		make_sample(true, 130, 25),
		make_sample(true, 90, 200),
	};
	EXPECT_EQ(ntp_select_best_sample(s, 3, 3000), 1);
}

TEST(NTPSelectBestSample, IgnoresFailedSamples)
{
	ntp_result_t s[3] = {
		make_sample(false, 0, 1), /* failed — tiny RTT must not win */
		make_sample(true, 50, 120),
		make_sample(false, 0, 2),
	};
	EXPECT_EQ(ntp_select_best_sample(s, 3, 3000), 1);
}

TEST(NTPSelectBestSample, RejectsAboveHardMax)
{
	/* Bufferbloat scenario: every sample's RTT is in the seconds —
	 * the offset error bound (±RTT/2) makes them all unusable. */
	ntp_result_t s[3] = {
		make_sample(true, 100, 4000),
		make_sample(true, 100, 5000),
		make_sample(true, 100, 3001),
	};
	EXPECT_EQ(ntp_select_best_sample(s, 3, 3000), -1);
}

TEST(NTPSelectBestSample, MixedQualityPicksAcceptableOne)
{
	ntp_result_t s[3] = {
		make_sample(true, 100, 4000), /* bufferbloat spike */
		make_sample(true, 102, 600),  /* acceptable */
		make_sample(true, 101, 3500), /* spike */
	};
	EXPECT_EQ(ntp_select_best_sample(s, 3, 3000), 1);
}

TEST(NTPSelectBestSample, EmptyAndNullAreRejected)
{
	ntp_result_t s[1] = {make_sample(true, 0, 10)};
	EXPECT_EQ(ntp_select_best_sample(s, 0, 3000), -1);
	EXPECT_EQ(ntp_select_best_sample(nullptr, 3, 3000), -1);
}

TEST(NTPSelectBestSample, CapDisabledWithNonPositiveMax)
{
	ntp_result_t s[1] = {make_sample(true, 100, 999999)};
	EXPECT_EQ(ntp_select_best_sample(s, 1, 0), 0);
	EXPECT_EQ(ntp_select_best_sample(s, 1, -1), 0);
}

TEST(NTPSelectBestSample, FirstOfEqualRoundtripsWins)
{
	ntp_result_t s[2] = {
		make_sample(true, 10, 50),
		make_sample(true, 20, 50),
	};
	EXPECT_EQ(ntp_select_best_sample(s, 2, 3000), 0);
}

/* ---- ntp_slew_step ---- */

TEST(NTPSlewStep, ZeroDiffIsNoOp)
{
	EXPECT_EQ(ntp_slew_step(0, 0, 1), 0);
	EXPECT_EQ(ntp_slew_step(500, 500, 10), 500);
	EXPECT_EQ(ntp_slew_step(-12345, -12345, 100), -12345);
}

TEST(NTPSlewStep, WithinStepSnapsToTarget)
{
	EXPECT_EQ(ntp_slew_step(0, 5, 10), 5);     /* +5 < +10 step */
	EXPECT_EQ(ntp_slew_step(100, 95, 10), 95); /* -5 within step */
	EXPECT_EQ(ntp_slew_step(0, 10, 10), 10);   /* exactly at step boundary */
	EXPECT_EQ(ntp_slew_step(0, -10, 10), -10);
}

TEST(NTPSlewStep, ExceedsStepPositiveDirection)
{
	EXPECT_EQ(ntp_slew_step(0, 5000, 1), 1);
	EXPECT_EQ(ntp_slew_step(0, 5000, 10), 10);
	EXPECT_EQ(ntp_slew_step(100, 5000, 1), 101);
}

TEST(NTPSlewStep, ExceedsStepNegativeDirection)
{
	EXPECT_EQ(ntp_slew_step(0, -5000, 1), -1);
	EXPECT_EQ(ntp_slew_step(0, -5000, 10), -10);
	EXPECT_EQ(ntp_slew_step(100, -5000, 1), 99);
}

TEST(NTPSlewStep, MaxStepZeroOrNegativeIsNoOp)
{
	EXPECT_EQ(ntp_slew_step(100, 500, 0), 100);
	EXPECT_EQ(ntp_slew_step(100, 500, -1), 100);
	EXPECT_EQ(ntp_slew_step(100, -500, 0), 100);
}

TEST(NTPSlewStep, LargeSignedValuesNoOverflow)
{
	/* applied near INT64 limits, target far away — must not overflow. */
	int64_t big_pos = (int64_t)1 << 60;
	int64_t big_neg = -big_pos;
	EXPECT_EQ(ntp_slew_step(big_neg, big_pos, 1), big_neg + 1);
	EXPECT_EQ(ntp_slew_step(big_pos, big_neg, 1), big_pos - 1);
	/* And with a step that lands exactly on the target should snap. */
	EXPECT_EQ(ntp_slew_step(big_pos - 5, big_pos, 5), big_pos);
}

TEST(NTPSlewStep, IdentityWhenAppliedEqualsTarget)
{
	/* Repeated calls converge in one step and stay put. */
	int64_t applied = 0;
	for (int i = 0; i < 5; i++) {
		applied = ntp_slew_step(applied, 3, 5);
	}
	EXPECT_EQ(applied, 3);
}
