#include <stdint.h>
#include <stdlib.h>

/* Portable, bounded binary long division. No division operator is used here:
 * the open toolchain's runtime omits the ARM EABI 64-bit divmod entry point.
 * Hidden linkage keeps this compatibility routine inside the candidate DSO. */
__attribute__((visibility("hidden")))
uint64_t cns2_u64_divmod(uint64_t numerator, uint64_t denominator,
                        uint64_t *remainder)
{
    uint64_t quotient = 0, rest = 0;
    int bit;
    if (!denominator) abort();
    for (bit = 63; bit >= 0; --bit) {
        unsigned int carry = (unsigned int)(rest >> 63);
        rest = (rest << 1) | ((numerator >> bit) & 1U);
        if (carry || rest >= denominator) {
            rest -= denominator;
            quotient |= UINT64_C(1) << bit;
        }
    }
    *remainder = rest;
    return quotient;
}
