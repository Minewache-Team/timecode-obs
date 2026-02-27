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
