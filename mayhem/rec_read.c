// Seed recorder (not part of build.sh): LD_PRELOAD shim that appends every byte read from fd 0 to $REC.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

ssize_t read(int fd, void* buf, size_t n) {
    static ssize_t (*real)(int, void*, size_t);
    if (!real) real = (ssize_t (*)(int, void*, size_t))dlsym(RTLD_NEXT, "read");
    ssize_t r = real(fd, buf, n);
    const char* path = getenv("REC");
    if (fd == 0 && r > 0 && path) {
        int out = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (out >= 0) { ssize_t w = write(out, buf, (size_t)r); (void)w; close(out); }
    }
    return r;
}

ssize_t __read_chk(int fd, void* buf, size_t n, size_t buflen) {
    (void)buflen;
    return read(fd, buf, n);
}
