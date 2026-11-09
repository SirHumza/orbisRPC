#ifndef ORBISRPC_CONFIG_LOCK_H
#define ORBISRPC_CONFIG_LOCK_H

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

/* Keep the sidecar inode across config renames; never unlink it on release. */
static inline int config_lock_acquire(const char *path){
    if(!path) return -1;
    char lock_path[512];
    int n = snprintf(lock_path, sizeof lock_path, "%s.lock", path);
    if(n < 0 || (size_t)n >= sizeof lock_path) return -1;
    int fd = open(lock_path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if(fd < 0) return -1;
    struct flock lock;
    memset(&lock, 0, sizeof lock);
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    while(fcntl(fd, F_SETLKW, &lock) < 0){
        if(errno == EINTR) continue;
        close(fd);
        return -1;
    }
    return fd;
}

static inline void config_lock_release(int fd){
    if(fd < 0) return;
    struct flock lock;
    memset(&lock, 0, sizeof lock);
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    (void)fcntl(fd, F_SETLK, &lock);
    (void)close(fd);
}

#endif
