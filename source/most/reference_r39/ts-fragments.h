#ifndef CNS2_TS_FRAGMENTS_H
#define CNS2_TS_FRAGMENTS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Native ES_To_TS emits header/PES/payload as separate writes (4+19+165,
 * then 4+184), with PAT/PMT/PCR as individual 188-byte writes. Never scan
 * inside arbitrary data for sync. Reconstruct only exact packet boundaries.
 * Connection identity, native callback origin, and loss generation are
 * checked by the caller; this is not a route proof on its own. */
struct cns2_ts_fragments { unsigned used, packets; unsigned char header[5]; };
static void cns2_fragments_reset(struct cns2_ts_fragments *s)
{ memset(s,0,sizeof(*s)); }
static int cns2_native_ts_caller(uintptr_t caller)
{
    return caller==0x00151060U || caller==0x001510fcU || caller==0x0015118cU;
}
static int cns2_fragments_feed(struct cns2_ts_fragments *s,const void *buffer,size_t size)
{
    const unsigned char *p=(const unsigned char *)buffer;
    unsigned completed=0;
    if(!p || !size || size>65536U || s->used>=188U)goto invalid;
    while(size){
        unsigned n=(unsigned)(size<188U-s->used?size:188U-s->used),i;
        for(i=0;i<n && s->used+i<5U;++i)s->header[s->used+i]=p[i];
        s->used+=n;p+=n;size-=n;
        if(s->used>=1U && s->header[0]!=0x47U)goto invalid;
        if(s->used>=2U && (s->header[1]&0x80U))goto invalid;
        if(s->used>=4U && ((s->header[3]&0xc0U) || !(s->header[3]&0x30U)))goto invalid;
        if(s->used>=5U){
            unsigned afc=(s->header[3]>>4)&3U;
            if((afc==2U && s->header[4]!=183U) || (afc==3U && s->header[4]>182U))goto invalid;
        }
        if(s->used==188U){
            s->used=0;++completed;
            if(s->packets<1000U)++s->packets;
        }
    }
    return (int)completed;
invalid:
    cns2_fragments_reset(s);return -1;
}
#endif
