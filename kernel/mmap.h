#include "types.h"

#define MAXMMAP 16

struct vmaInfo {
    uint64 addr;
    uint64 length;
    int prot;
    int flags;
    int fd;
    int offset;  
    int pid;
};
