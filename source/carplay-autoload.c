#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef CNS2_AUTOLOAD_LIBRARY
#error CNS2_AUTOLOAD_LIBRARY must be defined
#endif
#ifndef CNS2_AUTOLOAD_SYMBOL
#error CNS2_AUTOLOAD_SYMBOL must be defined
#endif
#ifndef CNS2_AUTOLOAD_LOG
#error CNS2_AUTOLOAD_LOG must be defined
#endif
#ifndef CNS2_AUTOLOAD_DELAY_MS
#define CNS2_AUTOLOAD_DELAY_MS 1500
#endif
#ifndef CNS2_AUTOLOAD_ATTEMPTS
#define CNS2_AUTOLOAD_ATTEMPTS 30
#endif

typedef int (*cns2_arm_fn_t)(void);
static void *g_patch_handle;

static void cns2_log(const char *message, int value)
{
    char line[320];
    int fd;
    int length = snprintf(line, sizeof(line), "%s value=%d errno=%d\n",
                          message, value, errno);
    if (length <= 0) return;
    if ((size_t)length > sizeof(line)) length = (int)sizeof(line);
    fd = open(CNS2_AUTOLOAD_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        (void)write(fd, line, (size_t)length);
        (void)close(fd);
    }
}

static void cns2_sleep_ms(unsigned milliseconds)
{
    struct timespec request;
    request.tv_sec = (time_t)(milliseconds / 1000U);
    request.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&request, &request) != 0 && errno == EINTR) {}
}

static void *cns2_autoload_thread(void *unused)
{
    unsigned attempt;
    (void)unused;
    cns2_sleep_ms(CNS2_AUTOLOAD_DELAY_MS);

    for (attempt = 1; attempt <= CNS2_AUTOLOAD_ATTEMPTS; ++attempt) {
        cns2_arm_fn_t arm;
        int rc;

        if (!g_patch_handle) {
            g_patch_handle = dlopen(CNS2_AUTOLOAD_LIBRARY, RTLD_NOW | RTLD_LOCAL);
            if (!g_patch_handle) {
                cns2_log("AUTOLOAD_DLOPEN_FAIL", (int)attempt);
                cns2_sleep_ms(1000U);
                continue;
            }
        }

        arm = (cns2_arm_fn_t)dlsym(g_patch_handle, CNS2_AUTOLOAD_SYMBOL);
        if (!arm) {
            cns2_log("AUTOLOAD_DLSYM_FAIL", (int)attempt);
            return NULL;
        }

        rc = arm();
        cns2_log("AUTOLOAD_ARM", rc);
        if (rc == 0 || rc == 1) return NULL;
        cns2_sleep_ms(1000U);
    }

    cns2_log("AUTOLOAD_GIVE_UP", CNS2_AUTOLOAD_ATTEMPTS);
    return NULL;
}

__attribute__((constructor))
static void cns2_autoload_start(void)
{
    pthread_t thread;
    pthread_attr_t attr;

    /* The loader has already consumed it; do not leak the preload to children. */
    (void)unsetenv("LD_PRELOAD");

    if (pthread_attr_init(&attr) != 0) {
        cns2_log("AUTOLOAD_ATTR_INIT_FAIL", -1);
        return;
    }
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, cns2_autoload_thread, NULL) != 0)
        cns2_log("AUTOLOAD_THREAD_FAIL", -1);
    (void)pthread_attr_destroy(&attr);
}
