#include <stdint.h>
extern uint64_t cns2_u64_divmod(uint64_t, uint64_t, uint64_t *);
/* Companion to the existing unsigned RTABI helper for this open compiler.
 * Unsigned subtraction also handles INT64_MIN without signed overflow. */
__attribute__((visibility("hidden")))
int64_t cns2_i64_divmod(int64_t numerator, int64_t denominator, int64_t *remainder)
{
    uint64_t n = (uint64_t)numerator, d = (uint64_t)denominator, r, q;
    if (numerator < 0) n = UINT64_C(0) - n;
    if (denominator < 0) d = UINT64_C(0) - d;
    q = cns2_u64_divmod(n, d, &r);
    if ((numerator < 0) != (denominator < 0)) q = UINT64_C(0) - q;
    if (numerator < 0) r = UINT64_C(0) - r;
    *remainder = (int64_t)r;
    return (int64_t)q;
}
