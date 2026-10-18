#ifndef CNS2_TRANSPORT_CLOCK_H
#define CNS2_TRANSPORT_CLOCK_H
#include <stdint.h>
/* PCR measures elapsed time, NOT successful byte count. A slow service or
 * bounded EAGAIN retry must not slow the decoder's clock. Call under the
 * core clock mutex; no OEM I/O or device control belongs in this helper. */
struct cns2_transport_clock {
    uint64_t origin_us, origin_90k, last_90k;
    int initialized;
};
static uint64_t cns2_clock_now(struct cns2_transport_clock *c,uint64_t now,
                              uint64_t initial_90k)
{
    uint64_t elapsed, value;
    if(!c->initialized){
        c->origin_us=now;c->origin_90k=c->last_90k=initial_90k;
        c->initialized=1;
    }
    elapsed=now>=c->origin_us?now-c->origin_us:0;
    value=c->origin_90k+(elapsed/1000000ULL)*90000ULL+
          ((elapsed%1000000ULL)*9ULL)/100ULL;
    if(value>c->last_90k)c->last_90k=value;
    return c->last_90k;
}
#endif
