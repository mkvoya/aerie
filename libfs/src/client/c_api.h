// The C front API of the library.
//
// This API helps the user to use both kernel and library file systems 
// transparently.

#ifndef __STAMNOS_FS_CLIENT_C_FRONT_API
#define __STAMNOS_FS_CLIENT_C_FRONT_API

#include <sys/types.h>
#include <stdint.h>
#include "rpc/rpc.h"
#include "rpc/jsl_log.h"

#define FRONTAPI(fname) libfs_##fname



int FRONTAPI(init) (char* xdst);
int FRONTAPI(shutdown) ();
int FRONTAPI(open) (const char* path, int flags);
int FRONTAPI(close) (int fd);
int FRONTAPI(dup) (int oldfd);
int FRONTAPI(dup2) (int oldfd, int newfd);
int FRONTAPI(mount) (const char* source, const char* target, const char* fstype, uint32_t flags);
int FRONTAPI(umount) (const char* target);
int FRONTAPI(mkfs) (const char* target, const char* fstype, uint32_t flags);

int FRONTAPI(mkdir) (const char* path, int mode);
int FRONTAPI(rmdir) (const char* path);
int FRONTAPI(chdir) (const char* path);
char* FRONTAPI(getcwd) (char* path, size_t size);

ssize_t FRONTAPI(write) (int fd, const void *buf, size_t count);
ssize_t FRONTAPI(read) (int fd, const void *buf, size_t count);
off_t FRONTAPI(lseek) (int fd, off_t offset, int whence);

#endif /* __STAMNOS_FS_CLIENT_C_FRONT_API_H */
