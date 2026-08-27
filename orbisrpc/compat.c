/* compat.c - tiny libc shims for the GoldHEN plugin link environment.
 * SceLibcInternal lacks getentropy/__errno_location which BearSSL's system
 * RNG seeder expects. Only compiled into the .prx build. */
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>

int *__errno_location(void){
    static int errno_slot;
    return &errno_slot;
}

int getentropy(void *buf, size_t n){
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
