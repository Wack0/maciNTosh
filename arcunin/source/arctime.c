#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "arc.h"
#include "arctime.h"
#include "timer.h"
#include "pxi.h"

enum {
	RTC_TO_UNIX_DIFF = 2082844800,
	SECONDS_PER_DAY = 24 * 60 * 60
};

static ULONG ArcGetRelativeTime(void) {
	return currmsecs() / 1000;
}

static TIME_FIELDS s_TimeFields = { 0 };

static bool IsLeapYear(ULONG Year) {
	return (!((Year) % 4) && ((Year) % 100) || !((Year) % 400));
}

static PTIME_FIELDS ArcGetTimeImpl(void) {
	//static UCHAR DaysPerMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	// last element is never used, so:
	static UCHAR DaysPerMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30 };
	ULONG Rtc = PxiRtcRead() - RTC_TO_UNIX_DIFF;
	ULONG Days = Rtc / SECONDS_PER_DAY;
	ULONG Remainder = Rtc % SECONDS_PER_DAY;

	s_TimeFields.Second = Remainder % 60;
	Remainder /= 60;
	s_TimeFields.Minute = Remainder % 60;
	s_TimeFields.Hour = Remainder / 60;
	
	// Calculate year
	ULONG Year = 1970;
	for (; Days >= 365 + (IsLeapYear(Year) ? 1 : 0); Year++) {
		Days -= 365 + (IsLeapYear(Year) ? 1 : 0);
	}

	s_TimeFields.Year = Year;
	s_TimeFields.Day = 1;
	if (IsLeapYear(Year) && Days >= 59) {
		if (Days == 59) s_TimeFields.Day++;
		Days--;
	}

	ULONG Month = 0;
	for (; Month < sizeof(DaysPerMonth) && Days >= (ULONG)DaysPerMonth[Month]; Month++)
		Days -= DaysPerMonth[Month];
	s_TimeFields.Month = Month;
	s_TimeFields.Day += Days;

	return &s_TimeFields;
}

static PTIME_FIELDS ArcGetTime(void) {
	PTIME_FIELDS impl = ArcGetTimeImpl();
	if (impl == NULL) {
		memset(&s_TimeFields, 0, sizeof(s_TimeFields));
		return &s_TimeFields;
	}
	return impl;
}

void ArcTimeInit(void) {
	// Initialise the functions implemented here.
	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
	Api->GetTimeRoutine = ArcGetTime;
	Api->GetRelativeTimeRoutine = ArcGetRelativeTime;
}