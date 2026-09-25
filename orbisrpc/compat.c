/* compat.c - tiny libc shims for the PS4 link environment.
 * SceLibcInternal lacks getentropy/__errno_location/gmtime_r, and
 * mbedTLS wants a platform entropy poll. Compiled into payload + plugin. */
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include "clock.h"

#ifdef ORBISRPC_SDK_PAYLOAD
/* Payload-SDK builds: the SDK libc already provides errno, gmtime_r,
 * clock_gettime and friends. Only the entropy poll + monotonic clock
 * live here, written against plain POSIX. */
int mbedtls_platform_entropy_poll(void *data, unsigned char *out,
                                  size_t len, size_t *olen){
    (void)data;
    if(!out || len == 0) return -1;
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd < 0) return -1;
    size_t got = 0;
    while(got < len){
        long r = read(fd, (char *)out + got, len - got);
        if(r <= 0){ close(fd); return -1; }
        got += (size_t)r;
    }
    close(fd);
    *olen = len;
    return 0;
}

int64_t orbis_mono_s(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return (int64_t)time(NULL);
    return (int64_t)ts.tv_sec;
}

#else

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
        if(r < 0 && errno == EINTR) continue; /* signal delivery, not failure */
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

int64_t orbis_mono_s(void){
#if defined(__PS4__) || (defined(__FreeBSD__) && !defined(__APPLE__))
    /* PS4 builds (--target=x86_64-pc-freebsd12-elf, -D__PS4__). Declared
     * here instead of including orbis headers so host builds stay clean. */
    extern unsigned long long sceKernelGetProcessTime(void);
    return (int64_t)(sceKernelGetProcessTime() / 1000000ull);
#else
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    ts.tv_sec = time(NULL);
    ts.tv_nsec = 0;
#endif
    return (int64_t)ts.tv_sec;
#endif
}

#endif /* ORBISRPC_SDK_PAYLOAD */
