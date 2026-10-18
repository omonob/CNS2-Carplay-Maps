#ifndef CNS2_STARTUP_WAIT_H
#define CNS2_STARTUP_WAIT_H
#include <stdint.h>
enum R43ImportState { R43_IMPORT_REFUSE = -1, R43_IMPORT_WAIT = 0,
                      R43_IMPORT_READY = 1, R43_IMPORT_OWNED = 2 };
/* No timeout counter. Known lazy import may resolve after any uptime.
 * Unknown or competing hooks are never overwritten/forcibly resolved. */
static int r43_import_state(uintptr_t value, uintptr_t native,
                            uintptr_t lazy, uintptr_t own)
{
    if (value == own) return R43_IMPORT_OWNED;
    if (value == native) return R43_IMPORT_READY;
    if (value == lazy) return R43_IMPORT_WAIT;
    return R43_IMPORT_REFUSE;
}
#endif
