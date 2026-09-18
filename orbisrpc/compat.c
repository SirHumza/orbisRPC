/* compat.c - tiny libc shims for the PS4 link environment.
 * SceLibcInternal lacks getentropy/__errno_location/gmtime_r, and
 * mbedTLS wants a platform entropy poll. Compiled into payload + plugin. */
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

int *__errno_location(void) __attribute__((weak));
int *__errno_location(void){
    static int errno_slot;
    return &errno_slot;
}

__attribute__((weak)) int getentropy(void *buf, size_t n){
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd < 0) return -1;
    size_t got = 0;
    while(got < n){
        long r = read(fd, (char*)buf + got, n - got);
        if(r <= 0){ close(fd); return -1; }
        got += (size_t)r;
    }
    close(fd);
    return 0;
}

__attribute__((weak)) struct tm *gmtime_r(const time_t *t, struct tm *r){
    /* Thread-safe: pure arithmetic, no static buffer (gmtime() is shared). */
    if(!t || !r) return NULL;
    long long secs = (long long)*t;
    long long days = secs / 86400LL;
    long long rem = secs % 86400LL;
    if(rem < 0){ rem += 86400LL; days--; }
    r->tm_hour = (int)(rem / 3600); rem %= 3600;
    r->tm_min = (int)(rem / 60);
    r->tm_sec = (int)(rem % 60);
    /* civil_from_days (Howard Hinnant) */
    long long z = days + 719468LL;
    long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    unsigned doe = (unsigned)(z - era * 146097LL);
    unsigned yoe = (doe - doe/1460U + doe/36524U - doe/146096U) / 365U;
    long long y = (long long)yoe + era * 400LL;
    unsigned doy = doe - (365U*yoe + yoe/4U - yoe/100U);
    unsigned mp = (5U*doy + 2U)/153U;
    unsigned d = doy - (153U*mp+2U)/5U + 1U;
    unsigned m = mp + (mp < 10U ? 3U : (unsigned)-9);
    y += (m <= 2);
    r->tm_mday = (int)d;
    r->tm_mon = (int)(m - 1);
    r->tm_year = (int)(y - 1900LL);
    /* weekday: 1970-01-01 was Thursday */
    r->tm_wday = (int)((days % 7 + 7 + 4) % 7);
    /* yearday */
    {
        int leap = ((y%4==0 && y%100!=0) || y%400==0);
        static const int cum[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
        r->tm_yday = cum[r->tm_mon] + (int)d - 1 + (leap && r->tm_mon > 1 ? 1 : 0);
    }
    r->tm_isdst = 0;
    return r;
}

/* mbedTLS platform entropy: /dev/urandom, the only strong source here. */
int mbedtls_platform_entropy_poll(void *data, unsigned char *out,
                                  size_t len, size_t *olen){
    (void)data;
    if(!out || len == 0) return -1;
    if(getentropy(out, len) != 0) return -1;
    *olen = len;
    return 0;
}
