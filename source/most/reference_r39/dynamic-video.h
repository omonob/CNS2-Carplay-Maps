#ifndef CNS2_DYNAMIC_VIDEO_H
#define CNS2_DYNAMIC_VIDEO_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#define CNS2_DYNAMIC_ABI 0x43563308U
#define CNS2_ROUTE_FRESH_MS 2000U
typedef ssize_t (*cns2_native_write)(int,const void *,size_t);
/* Loader owns connection discovery/lifetime. Core never guesses a descriptor.
 * write_video returns ESTALE on a retired route; it never opens a video device. */
struct cns2_dynamic_api {
    uint32_t abi, size;
    cns2_native_write native_write;
    int (*ready)(void);
    unsigned (*epoch)(void);
    int (*matches_stock)(int);
    ssize_t (*write_video)(unsigned,const void *,size_t);
    void *observer;
    void (*release_video)(unsigned,int);
    /* Explicit phone lifecycle, including disconnect AFTER writer failure.
     * Connection liveness and OEM picture cadence are independent. */
    void (*set_phone_session)(int);
};
static inline int cns2_ts_block(const void *buffer,size_t size)
{
    const unsigned char *p=(const unsigned char *)buffer;
    size_t offset;
    /* Whole aligned TS blocks, not a substring in a text/control message. */
    if(!p || size<3U*188U || size>65536U || size%188U) return 0;
    for(offset=0;offset<size;offset+=188U)
        if(p[offset]!=0x47 || (p[offset+1]&0x80) || !(p[offset+3]&0x30)) return 0;
    return 1;
}
#endif
