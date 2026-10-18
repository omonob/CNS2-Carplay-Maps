#ifndef CNS2_SEND_POLICY_H
#define CNS2_SEND_POLICY_H
#include <stdint.h>
/* Match existing PCR, 705/64 ticks per packet at 90 kHz. Late writes never
 * cause catch-up bursts; a stalled deadline is rebased to the real clock. */
struct cns2_pacer { uint64_t next_us; unsigned remainder; };
static void cns2_pacer_reset(struct cns2_pacer *p,uint64_t now)
{ p->next_us=now;p->remainder=0; }
static uint64_t cns2_pacer_delay(const struct cns2_pacer *p,uint64_t now)
{ return now<p->next_us?p->next_us-now:0; }
static void cns2_pacer_commit(struct cns2_pacer *p,uint64_t now,unsigned packets)
{
    uint64_t n=(uint64_t)packets*705ULL*1000000ULL+p->remainder;
    const unsigned denominator=64U*90000U;
    if(now>p->next_us)p->next_us=now;
    p->next_us+=n/denominator;p->remainder=(unsigned)(n%denominator);
}
/* Absolute fractional deadlines avoid accumulating the overshoot of every
 * QNX sleep. Retain at most ONE block of scheduling credit: a long write
 * drops all old debt rather than sending a chain of catch-up blocks. */
static void cns2_pacer_complete(struct cns2_pacer *p,uint64_t end,unsigned packets)
{
    uint64_t n=(uint64_t)packets*705ULL*1000000ULL+p->remainder;
    const unsigned denominator=64U*90000U;
    p->next_us+=n/denominator;p->remainder=(unsigned)(n%denominator);
    if(end>p->next_us)p->next_us=end;
}
#endif
