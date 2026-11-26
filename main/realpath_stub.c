// Rust's std (linked in from libyrs.a) calls realpath(), which newlib 4.6 does not provide: without
// this the link fails on an undefined reference. It only canonicalises paths the firmware never
// opens, so failing the way an unsupported call fails is enough.
#include <errno.h>

char *realpath(const char *path, char *resolved)
{
    (void)path;
    (void)resolved;
    errno = ENOSYS;
    return NULL;
}
