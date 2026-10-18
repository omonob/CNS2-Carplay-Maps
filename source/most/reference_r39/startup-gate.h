#ifndef CNS2_R32_STARTUP_GATE_H
#define CNS2_R32_STARTUP_GATE_H
#include <stdint.h>
/* Exact stock 0436 ELF and live libc mapping were verified independently.
 * The common resolver and function PLT are not native entrypoints. */
#define CNS2_WRITE_GOT_ADDR 0x0023fb58U
#define CNS2_CLOSE_GOT_ADDR 0x0023fb7cU
#define CNS2_WRITE_NATIVE_ADDR 0x0101a334U
#define CNS2_CLOSE_NATIVE_ADDR 0x010163d4U
#define CNS2_LAZY_RESOLVER_ADDR 0x00107800U
#define CNS2_WRITE_PLT_ADDR 0x00107da8U
#define CNS2_CLOSE_PLT_ADDR 0x00107e14U
enum cns2_startup_state {CNS2_STARTUP_REJECT=-1,CNS2_STARTUP_WAIT=0,CNS2_STARTUP_READY=1};
static int cns2_startup_state(uintptr_t write_got,uintptr_t close_got,
                              uintptr_t real_write,uintptr_t real_close)
{
    int write_pending,close_pending;
    /* Fail closed on a wrong firmware, missing library export, or other hook. */
    if(real_write!=CNS2_WRITE_NATIVE_ADDR || real_close!=CNS2_CLOSE_NATIVE_ADDR)
        return CNS2_STARTUP_REJECT;
    write_pending=write_got==CNS2_LAZY_RESOLVER_ADDR || write_got==CNS2_WRITE_PLT_ADDR;
    close_pending=close_got==CNS2_LAZY_RESOLVER_ADDR || close_got==CNS2_CLOSE_PLT_ADDR;
    if((write_got!=real_write && !write_pending) || (close_got!=real_close && !close_pending))
        return CNS2_STARTUP_REJECT;
    return (write_pending || close_pending)?CNS2_STARTUP_WAIT:CNS2_STARTUP_READY;
}
/* Roll back only our own close observer if write changes during installation.
 * CAS never overwrites somebody else's hook or a lazy resolver value. */
static int cns2_install_observers(uintptr_t *write_got,uintptr_t *close_got,
                                 uintptr_t real_write,uintptr_t real_close,
                                 uintptr_t write_hook,uintptr_t close_hook)
{
    if(cns2_startup_state(__atomic_load_n(write_got,__ATOMIC_ACQUIRE),
       __atomic_load_n(close_got,__ATOMIC_ACQUIRE),real_write,real_close)!=CNS2_STARTUP_READY)
        return 0;
    if(!__sync_bool_compare_and_swap(close_got,real_close,close_hook))return 0;
    if(!__sync_bool_compare_and_swap(write_got,real_write,write_hook)){
        (void)__sync_bool_compare_and_swap(close_got,close_hook,real_close);return 0;
    }
    return 1;
}
#endif
