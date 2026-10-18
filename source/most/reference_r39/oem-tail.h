#ifndef CNS2_OEM_TAIL_H
#define CNS2_OEM_TAIL_H
#include <stddef.h>
#include <string.h>
/* Capture ONLY real unsent bytes: a suffix of an accepted prefix, or one
 * whole OEM packet when its first fragment was not accepted at all.
 * Never fabricate padding, scan for 0x47, or clear physical wire accounting. */
static unsigned cns2_capture_tail(unsigned char *out,unsigned *known,
                                 unsigned needed,const void *data,size_t size)
{
    unsigned n;
    if(!out || !known || !data || *known>needed || needed>188U)return 0;
    n=(unsigned)(size<(size_t)(needed-*known)?size:(size_t)(needed-*known));
    memcpy(out+*known,data,n);*known+=n;return n;
}
#endif
