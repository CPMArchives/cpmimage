#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utime.h>
typedef __int64 ssize_t;
#define _POSIX_PATH_MAX _MAX_PATH
typedef int mode_t;
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#define DISKDEFS "diskdefs"
#define FORMAT "ibm-3740"
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
