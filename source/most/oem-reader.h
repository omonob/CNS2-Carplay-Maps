/* R43 borrows the original registered fd. NEVER dup, close, or change flags.
 * Stop/join this worker before native destroy. Two queued complete blocks
 * plus one assembling block bound OEM memory; only complete blocks drop.
 */
#ifndef CNS2_OEM_READER_H
#define CNS2_OEM_READER_H
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define OEM_READER_BLOCK (34U * 188U)
#ifndef OEM_READER_READ
#define OEM_READER_READ read
#endif
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    int active, finished, stop, error, drop, verified;
    int borrowed_fd, last_errno;
    ssize_t last_result;
    unsigned epoch, head, count, front_offset, partial_size, partial_offset;
    uint8_t blocks[2][OEM_READER_BLOCK], partial[OEM_READER_BLOCK];
    unsigned long bytes, discarded, starts;
} OemReader;
typedef struct {
    OemReader *reader; int fd; unsigned epoch, used;
    uint8_t block[OEM_READER_BLOCK];
} OemReadContext;
static OemReader r43_reader = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .changed = PTHREAD_COND_INITIALIZER,
    .borrowed_fd = -1
};
static void oem_reader_unlock(void *arg) { pthread_mutex_unlock(arg); }
static int oem_reader_valid_ts(const uint8_t *b)
{
    unsigned i;
    for (i = 0; i < OEM_READER_BLOCK; i += 188U)
        if (b[i] != 0x47) return 0;
    return 1;
}
static void oem_reader_cleanup(void *arg)
{
    OemReadContext *c = arg;
    OemReader *r = c->reader;
    pthread_mutex_lock(&r->mutex);
    if (c->epoch == r->epoch) {
        r->partial_size = c->used; r->partial_offset = 0;
        if (c->used) memcpy(r->partial, c->block, c->used);
        r->finished = 1; pthread_cond_broadcast(&r->changed);
    }
    pthread_mutex_unlock(&r->mutex);
    /* Original TSAPIL owns this fd even on failure. */
    free(c);
}
static void *oem_reader_thread(void *arg)
{
    OemReadContext *c = arg;
    OemReader *r = c->reader;
    int old;
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_cleanup_push(oem_reader_cleanup, c);
    for (;;) {
        ssize_t n;
        int read_errno;
        pthread_testcancel();
        errno = 0;
        n = OEM_READER_READ(c->fd, c->block + c->used, sizeof(c->block) - c->used);
        read_errno = n < 0 ? errno : 0; /* Capture BEFORE pthread calls. */
        if (n < 0 && read_errno == EINTR) continue;
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old);
        pthread_mutex_lock(&r->mutex);
        r->last_result = n; r->last_errno = read_errno;
        if (n > 0 && (size_t)n <= sizeof(c->block) - c->used) {
            c->used += (unsigned)n; r->bytes += (unsigned)n;
        } else {
            r->error = n < 0 ? (read_errno ? read_errno : EIO) :
                       n == 0 ? EPIPE : EOVERFLOW;
        }
        if (r->stop || r->epoch != c->epoch || r->error) {
            pthread_cond_broadcast(&r->changed);
            pthread_mutex_unlock(&r->mutex);
            pthread_setcancelstate(old, NULL); break;
        }
        if (c->used == sizeof(c->block)) {
            if (!oem_reader_valid_ts(c->block)) {
                r->error = EPROTO; pthread_cond_broadcast(&r->changed);
                pthread_mutex_unlock(&r->mutex);
                pthread_setcancelstate(old, NULL); break;
            }
            r->verified = 1;
            while (r->count == 2 && !r->stop && !r->drop)
                pthread_cond_wait(&r->changed, &r->mutex);
            if (!r->stop) {
                if (r->count == 2) {
                    r->head = (r->head + 1U) % 2U; --r->count; ++r->discarded;
                }
                memcpy(r->blocks[(r->head + r->count) % 2U], c->block, sizeof(c->block));
                ++r->count; c->used = 0; pthread_cond_broadcast(&r->changed);
            }
        }
        pthread_mutex_unlock(&r->mutex); pthread_setcancelstate(old, NULL);
    }
    pthread_cleanup_pop(1);
    return NULL;
}
/* Only caller's worker is canceled. No native fd/process is closed/killed.
 * Error fallback preserves already consumed bytes; destruction drops OLD
 * route bytes before stock closes that route's descriptor.
 */
static void oem_reader_quiesce(OemReader *r, int preserve)
{
    pthread_t worker;
    int active, finished;
    pthread_mutex_lock(&r->mutex);
    active = r->active; worker = r->thread; finished = r->finished;
    r->stop = 1; pthread_cond_broadcast(&r->changed);
    pthread_mutex_unlock(&r->mutex);
    if (active) {
        if (!finished) pthread_cancel(worker);
        pthread_join(worker, NULL);
    }
    pthread_mutex_lock(&r->mutex);
    r->active = 0; r->verified = 0; r->borrowed_fd = -1;
    if (!preserve) {
        r->count = r->head = r->front_offset = 0;
        r->partial_size = r->partial_offset = 0;
    }
    pthread_mutex_unlock(&r->mutex);
}
static void oem_reader_stop(OemReader *r) { oem_reader_quiesce(r, 0); }
static int oem_reader_start(OemReader *r, int native_fd)
{
    OemReadContext *c;
    int rc;
    if (native_fd < 0) return EINVAL;
    c = calloc(1, sizeof(*c)); if (!c) return ENOMEM;
    c->fd = native_fd; c->reader = r;
    pthread_mutex_lock(&r->mutex);
    if (r->active) { pthread_mutex_unlock(&r->mutex); free(c); return EBUSY; }
    r->stop = r->error = r->drop = r->verified = r->finished = 0;
    r->head = r->count = r->front_offset = r->partial_size = r->partial_offset = 0;
    r->last_result = 0; r->last_errno = 0;
    c->epoch = ++r->epoch; r->borrowed_fd = native_fd;
    rc = pthread_create(&r->thread, NULL, oem_reader_thread, c);
    r->active = !rc; if (!rc) ++r->starts; else r->borrowed_fd = -1;
    pthread_mutex_unlock(&r->mutex);
    if (rc) free(c);
    return rc;
}
static void oem_reader_drop(OemReader *r, int drop)
{
    pthread_mutex_lock(&r->mutex); r->drop = drop;
    pthread_cond_broadcast(&r->changed); pthread_mutex_unlock(&r->mutex);
}
static int oem_reader_error(OemReader *r)
{
    int error;
    pthread_mutex_lock(&r->mutex);
    error = r->error ? r->error : r->stop ? ECANCELED : 0;
    pthread_mutex_unlock(&r->mutex); return error;
}
static int oem_reader_verified(OemReader *r)
{
    int verified;
    pthread_mutex_lock(&r->mutex); verified = r->verified && !r->error && !r->stop;
    pthread_mutex_unlock(&r->mutex); return verified;
}
static int oem_reader_pop(OemReader *r, void *out, unsigned epoch)
{
    int rc;
    pthread_mutex_lock(&r->mutex);
    pthread_cleanup_push(oem_reader_unlock, &r->mutex);
    while (!r->count && !r->stop && !r->error && !r->drop && r->epoch == epoch)
        pthread_cond_wait(&r->changed, &r->mutex);
    rc = r->stop || r->epoch != epoch ? ECANCELED : r->error ? r->error : r->drop ? EAGAIN : 0;
    if (!rc) {
        memcpy(out, r->blocks[r->head], OEM_READER_BLOCK);
        r->head = (r->head + 1U) % 2U; --r->count; pthread_cond_broadcast(&r->changed);
    }
    pthread_cleanup_pop(1); return rc;
}
/* Quiesced SAME-route OEM bytes, including partial reads, are drained before
 * the native sender reads the original descriptor again. */
static unsigned oem_reader_drain(OemReader *r, void *out, unsigned limit)
{
    unsigned used = 0;
    pthread_mutex_lock(&r->mutex);
    if (r->active) { pthread_mutex_unlock(&r->mutex); return 0; }
    while (used < limit && (r->count || r->partial_offset < r->partial_size)) {
        unsigned n, remain; uint8_t *src;
        if (r->count) {
            remain = OEM_READER_BLOCK - r->front_offset;
            src = r->blocks[r->head] + r->front_offset;
        } else {
            remain = r->partial_size - r->partial_offset;
            src = r->partial + r->partial_offset;
        }
        n = limit - used; if (n > remain) n = remain;
        memcpy((uint8_t *)out + used, src, n); used += n;
        if (r->count) {
            r->front_offset += n;
            if (r->front_offset == OEM_READER_BLOCK) {
                r->front_offset = 0; r->head = (r->head + 1U) % 2U; --r->count;
            }
        } else r->partial_offset += n;
    }
    pthread_mutex_unlock(&r->mutex); return used;
}
#endif
