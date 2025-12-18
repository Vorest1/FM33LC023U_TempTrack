#ifndef RTC_BUILD_TIME_LL_H
#define RTC_BUILD_TIME_LL_H

#include <stdint.h>
#include <stdio.h>
#include "fm33lc0xx_ll_rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTC_BASE_ADDR      (0x40011000UL)
#define RTC_REG32(off)     (*((volatile unsigned int *)(RTC_BASE_ADDR + (off))))
#define RTC_WER            RTC_REG32(0x00)
#define RTC_BCDSEC         RTC_REG32(0x0C)
#define RTC_BCDMIN         RTC_REG32(0x10)
#define RTC_BCDHOUR        RTC_REG32(0x14)
#define RTC_BCDDAY         RTC_REG32(0x18)
#define RTC_BCDWEEK        RTC_REG32(0x1C)
#define RTC_BCDMONTH       RTC_REG32(0x20)
#define RTC_BCDYEAR        RTC_REG32(0x24)
#define RTC_BKR0           RTC_REG32(0x70)

#define RTC_WER_UNLOCK_KEY (0xACACACACUL)

/* RTC register access (FM33LC0xx): base 0x40011000, see product manual table “RTC(??????:0x40011000)”. */

/* BKR0 stores a build-derived signature so RTC gets initialized once per firmware build.
 * This lets you reflash a new build and automatically reset RTC to the new build time,
 * while keeping RTC running across DeepSleep wake-ups.
 */

static unsigned char dec2(const char *p)
{
    return (unsigned char)(((unsigned int)(p[0] - '0') * 10u) + (unsigned int)(p[1] - '0'));
}

static unsigned char month_from_abbr3(const char *m3)
{
    /* __DATE__ uses English month abbreviations: "Jan", "Feb", ... */
    if (memcmp(m3, "Jan", 3) == 0) return 1;
    if (memcmp(m3, "Feb", 3) == 0) return 2;
    if (memcmp(m3, "Mar", 3) == 0) return 3;
    if (memcmp(m3, "Apr", 3) == 0) return 4;
    if (memcmp(m3, "May", 3) == 0) return 5;
    if (memcmp(m3, "Jun", 3) == 0) return 6;
    if (memcmp(m3, "Jul", 3) == 0) return 7;
    if (memcmp(m3, "Aug", 3) == 0) return 8;
    if (memcmp(m3, "Sep", 3) == 0) return 9;
    if (memcmp(m3, "Oct", 3) == 0) return 10;
    if (memcmp(m3, "Nov", 3) == 0) return 11;
    if (memcmp(m3, "Dec", 3) == 0) return 12;
    return 1; /* fallback */
}

static unsigned char weekday_iso_1_7(unsigned int y, unsigned int m, unsigned int d)
{
    /* Sakamoto algorithm: returns 0=Sunday..6=Saturday */
    static const unsigned char t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y -= 1;
    unsigned int dow = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    /* Convert to ISO-like 1..7 with Monday=1, Sunday=7 */
    if (dow == 0) return 7;
    return (unsigned char)dow; /* 1..6 */
}

static void RTC_ParseBuildDateTime(unsigned int *year_full, unsigned char *mon,
                                  unsigned char *day, unsigned char *hh,
                                  unsigned char *mm, unsigned char *ss)
{
    /* __DATE__ = "Mmm dd yyyy" (day may have leading space), __TIME__ = "hh:mm:ss" */
    const char *d = __DATE__;
    const char *t = __TIME__;

    unsigned char m = month_from_abbr3(d);
    unsigned char dd = (d[4] == ' ') ? (unsigned char)(d[5] - '0') : (unsigned char)(((d[4] - '0') * 10) + (d[5] - '0'));
    unsigned int yyyy = (unsigned int)(d[7] - '0') * 1000u + (unsigned int)(d[8] - '0') * 100u + (unsigned int)(d[9] - '0') * 10u + (unsigned int)(d[10] - '0');

    if (year_full) *year_full = yyyy;
    if (mon) *mon = m;
    if (day) *day = dd;
    if (hh) *hh = dec2(&t[0]);
    if (mm) *mm = dec2(&t[3]);
    if (ss) *ss = dec2(&t[6]);
}

static unsigned int fnv1a32_step(unsigned int h, const char *s)
{
    while (*s) {
        h ^= (unsigned int)(unsigned char)(*s++);
        h *= 16777619u;
    }
    return h;
}

static unsigned int RTC_BuildSignature(void)
{
    /* 32-bit signature derived from build date/time strings.
     * Changes on each build, so RTC gets re-initialized after reflashing.
     */
    unsigned int h = 2166136261u;
    h = fnv1a32_step(h, __DATE__);
    h = fnv1a32_step(h, " ");
    h = fnv1a32_step(h, __TIME__);
    return (0x52544300u ^ h);
}

static unsigned char bcd2bin(unsigned char v)
{
    return (unsigned char)(((v >> 4) * 10u) + (v & 0x0Fu));
}

static unsigned char bin2bcd(unsigned char v)
{
    return (unsigned char)((((unsigned int)v / 10u) << 4) | ((unsigned int)v % 10u));
}

static void RTC_SimpleInit_IfNeeded(void)
{
    /* RTC keeps running in DeepSleep; we initialize it once per firmware build.
     * Note: this sets RTC to the *build machine's* local time at compile-time.
     */
    unsigned int sig = RTC_BuildSignature();
    if (RTC_BKR0 == sig) {
        return;
    }

    unsigned int year_full = 2025;
    unsigned char mon = 1, day = 1, hh = 0, mm = 0, ss = 0;
    RTC_ParseBuildDateTime(&year_full, &mon, &day, &hh, &mm, &ss);
    unsigned char week = weekday_iso_1_7(year_full, mon, day);

    RTC_WER = RTC_WER_UNLOCK_KEY;
    RTC_BCDSEC   = bin2bcd(ss);
    RTC_BCDMIN   = bin2bcd(mm);
    RTC_BCDHOUR  = bin2bcd(hh);
    RTC_BCDDAY   = bin2bcd(day);
    RTC_BCDWEEK  = bin2bcd(week);
    RTC_BCDMONTH = bin2bcd(mon);
    RTC_BCDYEAR  = bin2bcd((unsigned char)(year_full % 100u));
    RTC_WER = 0;

    RTC_BKR0 = sig;
}

static void RTC_ReadTimeHMS(unsigned char *hh, unsigned char *mm, unsigned char *ss)
{
    /* Read-stability method from manual: read twice until equal. */
    unsigned int s1, m1, h1;
    unsigned int s2, m2, h2;

    do {
        s1 = RTC_BCDSEC;
        m1 = RTC_BCDMIN;
        h1 = RTC_BCDHOUR;
        s2 = RTC_BCDSEC;
        m2 = RTC_BCDMIN;
        h2 = RTC_BCDHOUR;
    } while ((s1 != s2) || (m1 != m2) || (h1 != h2));

    if (ss) *ss = bcd2bin((unsigned char)(s1 & 0xFFu));
    if (mm) *mm = bcd2bin((unsigned char)(m1 & 0xFFu));
    if (hh) *hh = bcd2bin((unsigned char)(h1 & 0xFFu));
}
																	
#ifdef __cplusplus
}
#endif

#endif /* RTC_BUILD_TIME_LL_H */
