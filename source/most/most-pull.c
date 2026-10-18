/* R43 OFFLINE candidate: preserve original registered coid, single native
 * MLB writer, fault latch, and late-import wait. Borrowed reader is joined
 * BEFORE original connection destruction and never closes its descriptor.
 * R41 identifiers below are retained shared codec internals, not validation.
 */
#define CNS2_NAVINTERFACE_HOTLOAD 1
#define CNS2_LOG_PATH "/ramdisk/cns2-most-r43.log"
#define TS_QUEUE_PACKETS 4096U
#include "reference_r39/nav-bridge.c"
#include <dlfcn.h>
#include "oem-reader.h"
#include "startup-wait.h"

#define R41_BLOCK_BYTES (34U * TS_SIZE)
#define R41_RECEIVE_PC ((uintptr_t)0x001bdd34U)
#define R41_RECEIVE_GOT ((uintptr_t)0x0024ad2cU)
#ifndef R41_HOST_TEST
#define R41_VIDEO_BUFFER ((uintptr_t)0x00251c88U)
#else
static uint8_t r43_host_native_buffer[R41_BLOCK_BYTES];
#define R41_VIDEO_BUFFER ((uintptr_t)r43_host_native_buffer)
#endif
#define R41_ACTIVE_ENV "CNS2_MOST_R43_ACTIVE"
#define R41_DISABLE "/persistence/cns275/most-r43/DISABLED"
#define R43_DESTROY_GOT ((uintptr_t)0x0024ad74U)

typedef int (*r41_receive_fn)(void *, void *, unsigned, unsigned *);
static r41_receive_fn r41_native_receive;
static int r41_ok, r41_incomplete, r43_error;
typedef void *(*r43_handle_fn)(void *);
typedef int (*r43_fd_fn)(void *);
typedef void (*r43_destroy_fn)(void *);
static r43_handle_fn r43_get_ts, r43_get_io;
static r43_fd_fn r43_get_fd;
static r43_destroy_fn r43_native_destroy;
static void *r43_proxy_handle;
static int r43_proxy_fd = -1;
static unsigned r43_proxy_epoch;
static uint64_t r43_last_supply_us;
static unsigned long r43_independent_calls, r43_proxy_errors;
static uint64_t r43_gap_max_us;
static unsigned long r43_slow_supply_calls;
static unsigned r43_faulted, r43_start_attempts, r43_arm_wait_polls;
static int r43_fault_errno, r43_fault_read_errno, r43_arm_state;
static ssize_t r43_fault_read_result;
static pthread_mutex_t r41_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned r41_hooked, r41_phone, r41_owned, r41_returning;
static unsigned r41_disabled, r41_epoch = 1, r41_seen_ready;
static uint64_t r41_last_receive_us, r41_next_psi_us, r41_next_pcr_us;
static void *r41_handle;
static uint8_t r41_spill[TS_SIZE];
static size_t r41_spill_offset = TS_SIZE;
static unsigned r41_pmt_pending;
static uint8_t r41_oem_tail[TS_SIZE];
static unsigned r41_oem_tail_size;
static unsigned long r41_calls, r41_native_bytes, r41_phone_bytes;
static unsigned long r41_acquires, r41_returns, r41_bad_calls, r41_route_resets;
static void r433_intro_abort(const char *reason);
static int r433_source_eligible(void);

/* All codec state and this ownership state are serialized by r41_mutex.
 * No hook holds that mutex while making the native (possibly blocking) call.
 */
static int r41_ready(void)
{
    return r41_hooked && !r41_disabled && !r43_faulted &&
           r43_proxy_handle && oem_reader_verified(&r43_reader);
}
static unsigned r41_get_epoch(void) { return r41_epoch; }
static const struct cns2_dynamic_api r41_codec_api = {
    .abi = CNS2_DYNAMIC_ABI, .size = sizeof(struct cns2_dynamic_api),
    .ready = r41_ready, .epoch = r41_get_epoch
};

static int r41_au_in_progress(void)
{
    int in_au;
    pthread_mutex_lock(&g_queue.mutex);
    in_au = g_queue.in_au;
    pthread_mutex_unlock(&g_queue.mutex);
    return in_au;
}

static void r41_request_return(const char *reason)
{
    r433_intro_abort(reason);
    r41_phone = 0;
    g_takeover = 0;
    g_waiting_for_keyframe = 1;
    g_recovery_idr_requested = 0;
    if (r41_owned) {
        if (!r41_returning) {
            /* Keep only the AU already started on this same native route. */
            size_t discarded = queue_discard_all(&g_queue);
            r41_returning = 1;
            fprintf(stderr, "R41_RETURN_PENDING reason=%s discarded=%lu spill=%lu\n",
                    reason, (unsigned long)discarded,
                    (unsigned long)r41_spill_offset);
        }
    } else {
        (void)queue_abort_all(&g_queue);
    }
}

/* Called under ownership mutex; worker never takes this mutex. Stop wakes
 * every proxy pop before cancelling/joining ONLY our own reader thread.
 */
static void r43_stop_proxy(void)
{
    oem_reader_stop(&r43_reader);
    r43_proxy_handle = NULL; r43_proxy_fd = -1;
    r43_last_supply_us = 0;
}

static void r41_reset_route(const char *reason)
{
    r433_intro_abort(reason);
    r43_stop_proxy();
    /* Native errors/new handles start a new native receive stream. The old
     * route's fragment must not be sent under the next connection's identity.
     */
    r41_owned = r41_returning = r41_seen_ready = 0;
    r41_spill_offset = TS_SIZE;
    r41_pmt_pending = 0;
    r41_oem_tail_size = 0;
    g_takeover = 0;
    g_waiting_for_keyframe = 1;
    g_last_sequence = 0;
    g_recovery_idr_requested = 0;
    g_media_origin_ntp = g_local_origin_us = 0;
    g_discontinuity_pending = 1;
    (void)queue_abort_all(&g_queue);
    ++r41_epoch;
    ++r41_route_resets;
    fprintf(stderr, "R41_ROUTE_RESET reason=%s epoch=%u\n", reason, r41_epoch);
}

static void r41_next_packet(uint8_t packet[TS_SIZE])
{
    uint64_t now = monotonic_us();
    if (r41_pmt_pending) {
        (void)make_pmt_packet(packet);
        r41_pmt_pending = 0;
    } else if (now >= r41_next_psi_us) {
        (void)make_pat_packet(packet);
        r41_pmt_pending = 1;
        r41_next_psi_us = now + 40000ULL;
    } else if (now >= r41_next_pcr_us) {
        make_pcr_packet(packet, transport_now_at(now));
        r41_next_pcr_us = now + 40000ULL;
    } else if (!queue_pop(&g_queue, packet)) {
        make_null_packet(packet);
    }
    ++g_ts_packets_total;
}

static void r41_fill(uint8_t *out, size_t size)
{
    while (size) {
        size_t n;
        if (r41_spill_offset == TS_SIZE) {
            r41_next_packet(r41_spill);
            r41_spill_offset = 0;
        }
        n = TS_SIZE - r41_spill_offset;
        if (n > size) n = size;
        memcpy(out, r41_spill + r41_spill_offset, n);
        out += n;
        size -= n;
        r41_spill_offset += n;
    }
}

/* Returns 1 only if this successful OEM read was replaced. Caller provides
 * the exact native prefix offset (native preserves <188 trailing bytes).
 */
static int r41_replace(void *handle, uint8_t *buffer, unsigned offset,
                       unsigned count, uint64_t now)
{
    size_t expected_spill;
    if (handle != r41_handle) {
        if (r41_handle) r41_reset_route("native-handle-changed");
        r41_handle = handle;
    }
    r41_last_receive_us = now;
    r41_seen_ready = 1;
    if (r41_disabled && !r41_returning) r41_request_return("disabled");
    if (r41_returning && !queue_count(&g_queue) &&
        !r41_au_in_progress() && r41_spill_offset == TS_SIZE && !offset) {
        r41_owned = r41_returning = 0;
        ++r41_returns;
        fprintf(stderr, "R41_OEM_RETURN native_boundary=0 bytes=%lu returns=%lu\n",
                r41_native_bytes, r41_returns);
        return 0;
    }
    if (!r41_owned) {
        unsigned whole = (count / TS_SIZE) * TS_SIZE;
        if (r41_disabled || !r433_source_eligible() || !g_takeover || r41_returning ||
            offset || whole < 3U * TS_SIZE ||
            !cns2_ts_block(buffer, whole) || !queue_count(&g_queue)) return 0;
        r41_owned = 1;
        r41_spill_offset = TS_SIZE;
        r41_next_psi_us = r41_next_pcr_us = 0;
        r41_pmt_pending = 0;
        __atomic_store_n(&g_pcr_discontinuity, 1U, __ATOMIC_RELEASE);
        ++r41_acquires;
        fprintf(stderr, "R41_PHONE_ACQUIRE epoch=%u queue=%lu count=%u\n",
                r41_epoch, (unsigned long)queue_count(&g_queue), count);
    }
    expected_spill = r41_spill_offset == TS_SIZE ? 0 : r41_spill_offset;
    if (expected_spill != offset) {
        ++r41_bad_calls;
        r41_reset_route("native-fragment-offset-mismatch");
        /* Native might hold a phone partial packet. Restore its <188-byte
         * prefix from the saved native bytes before letting OEM continue.
         * The hook's caller must do this repair, not fabricate successful I/O.
         */
        return -1;
    }
    r41_fill(buffer, count);
    r41_phone_bytes += count;
    return 1;
}

/* Fail closed for the whole process lifetime, not an automatic reconnect.
 * At this hook all independent output blocks end on a TS boundary. No old
 * phone bytes are relabeled as OEM; same-route consumed OEM bytes drain first.
 */
static void r43_latch_fault(int error, const char *reason)
{
    if (r43_faulted) return;
    r43_faulted = 1; r43_fault_errno = error; ++r43_proxy_errors;
    pthread_mutex_lock(&r43_reader.mutex);
    r43_fault_read_result = r43_reader.last_result;
    r43_fault_read_errno = r43_reader.last_errno;
    pthread_mutex_unlock(&r43_reader.mutex);
    fprintf(stderr, "R43_FAULT_LATCH reason=%s error=%d read_result=%ld read_errno=%d attempts=%u fd=%d\n",
            reason, error, (long)r43_fault_read_result, r43_fault_read_errno,
            r43_start_attempts, r43_proxy_fd);
    oem_reader_quiesce(&r43_reader, 1);
    r43_proxy_handle = NULL; r43_proxy_fd = -1;
    r41_phone = r41_owned = r41_returning = r41_seen_ready = 0;
    g_takeover = 0; g_waiting_for_keyframe = g_discontinuity_pending = 1;
    r41_spill_offset = TS_SIZE; r41_pmt_pending = 0;
    (void)queue_abort_all(&g_queue);
}
static int r43_fallback_receive(void *handle, void *buffer, unsigned request,
                                unsigned *received)
{
    unsigned n = 0;
    uintptr_t ptr = (uintptr_t)buffer;
    int valid = received && ptr >= R41_VIDEO_BUFFER &&
                ptr < R41_VIDEO_BUFFER + TS_SIZE && request &&
                request == R41_BLOCK_BYTES - (ptr - R41_VIDEO_BUFFER);
    pthread_mutex_lock(&r41_mutex);
    if (handle == r41_handle && valid)
        n = oem_reader_drain(&r43_reader, buffer, request);
    pthread_mutex_unlock(&r41_mutex);
    if (n) {
        *received = n;
        return n == request ? r41_ok : r41_incomplete;
    }
    return r41_native_receive(handle, buffer, request, received);
}
static int r41_receive_hook(void *handle, void *buffer, unsigned request,
                            unsigned *received)
{
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
#ifdef R41_HOST_TEST
    caller = R41_RECEIVE_PC;
#endif
    int result;
    uintptr_t ptr = (uintptr_t)buffer;
    unsigned offset, count, total, tail, native_epoch;
    int replaced;
    unsigned was_owned;
    if (caller != R41_RECEIVE_PC)
        return r41_native_receive(handle, buffer, request, received);
r43_retry:
    /* R43's proxy is armed only at an observed complete native block. From
     * then on the original sender consumes exactly one complete TS block per
     * call. Native transmit still retries its real DMA busy state; this is
     * not a second hardware sender or a fabricated successful hardware write.
     */
    pthread_mutex_lock(&r41_mutex);
    if (r41_handle && r41_handle != handle) {
        r41_reset_route("native-handle-changed");
        r41_handle = handle;
    }
    /* Track even the first blocking native read. Destruction during it must
     * invalidate its result BEFORE accessors touch a possibly freed handle. */
    if (!r41_handle) r41_handle = handle;
    if (r43_faulted) {
        pthread_mutex_unlock(&r41_mutex);
        return r43_fallback_receive(handle, buffer, request, received);
    }
    if (r43_proxy_handle && (r43_proxy_handle != handle ||
        !received || ptr != R41_VIDEO_BUFFER || request != R41_BLOCK_BYTES)) {
        r43_latch_fault(EPROTO, "proxy-route-or-request-changed");
        pthread_mutex_unlock(&r41_mutex);
        return r43_fallback_receive(handle, buffer, request, received);
    }
    if (r43_proxy_handle == handle && received &&
        ptr == R41_VIDEO_BUFFER && request == R41_BLOCK_BYTES) {
        unsigned epoch = r43_proxy_epoch;
        uint64_t deadline = r43_last_supply_us + 4162ULL;
        int supply, error = oem_reader_error(&r43_reader);
        if (error) {
            r43_latch_fault(error, "OEM-reader-error");
            pthread_mutex_unlock(&r41_mutex);
            return r43_fallback_receive(handle, buffer, request, received);
        }
        r41_last_receive_us = monotonic_us(); r41_seen_ready = 1;
        if (r41_disabled && !r41_returning) r41_request_return("disabled");
        if (r41_returning && !queue_count(&g_queue) &&
            !r41_au_in_progress() && r41_spill_offset == TS_SIZE) {
            r41_owned = r41_returning = 0; ++r41_returns;
            fprintf(stderr, "R43_OEM_RETURN returns=%lu\n", r41_returns);
        }
        supply = oem_reader_verified(&r43_reader) && (r41_owned || (!r41_disabled && r433_source_eligible() &&
                 g_takeover && !r41_returning && queue_count(&g_queue)));
        oem_reader_drop(&r43_reader, supply);
        pthread_mutex_unlock(&r41_mutex);
        if (supply) {
            /* Nominal 12.288 Mbit/s is an upper limit, NOT an assumed measured
             * CNS2 rate. Hardware busy retries can slow us further. Rebase
             * after every delayed call; never burst to catch up lost time.
             */
            uint64_t now = monotonic_us();
            if (now < deadline) usleep((useconds_t)(deadline - now));
            pthread_mutex_lock(&r41_mutex);
            if (r43_proxy_handle != handle || r43_proxy_epoch != epoch) {
                *received = 0;
                pthread_mutex_unlock(&r41_mutex);
                return r43_error;
            }
            error = oem_reader_error(&r43_reader);
            if (error) {
                r43_latch_fault(error, "OEM-reader-error-during-pacing");
                pthread_mutex_unlock(&r41_mutex);
                return r43_fallback_receive(handle, buffer, request, received);
            }
            if (!r41_owned && (!r433_source_eligible() || !g_takeover ||
                r41_disabled || r41_returning || !queue_count(&g_queue))) {
                pthread_mutex_unlock(&r41_mutex);
                goto r43_retry;
            }
            /* Disconnect can arrive during pacing; finish a started AU and
             * return on next block. All provided bytes are real mux output.
             */
            if (!r41_owned) {
                r41_owned = 1; r41_spill_offset = TS_SIZE;
                r41_next_psi_us = r41_next_pcr_us = 0;
                r41_pmt_pending = 0; ++r41_acquires;
                __atomic_store_n(&g_pcr_discontinuity, 1U, __ATOMIC_RELEASE);
                fprintf(stderr, "R43_PHONE_ACQUIRE epoch=%u\n", r41_epoch);
            }
            r41_fill(buffer, request); *received = request;
            r41_phone_bytes += request; ++r41_calls; ++r43_independent_calls;
            now = monotonic_us();
            if (r43_last_supply_us) {
                uint64_t gap = now - r43_last_supply_us;
                if (gap > r43_gap_max_us) r43_gap_max_us = gap;
                if (gap > 60000ULL) ++r43_slow_supply_calls;
            }
            r43_last_supply_us = r41_last_receive_us = now;
            pthread_mutex_unlock(&r41_mutex);
            return r41_ok;
        }
        /* Wakeable OEM passthrough. Reader cancellation or route generation
         * change cannot deliver an old fd's bytes onto a new connection.
         */
        error = oem_reader_pop(&r43_reader, buffer, epoch);
        pthread_mutex_lock(&r41_mutex);
        if (error == EAGAIN && r43_proxy_handle == handle && r43_proxy_epoch == epoch) {
            pthread_mutex_unlock(&r41_mutex);
            goto r43_retry;
        }
        if (r43_proxy_handle != handle || r43_proxy_epoch != epoch) {
            *received = 0;
            pthread_mutex_unlock(&r41_mutex);
            return r43_error;
        }
        if (error) {
            r43_latch_fault(error, "OEM-reader-pop-error");
            pthread_mutex_unlock(&r41_mutex);
            return r43_fallback_receive(handle, buffer, request, received);
        }
        *received = request; r41_native_bytes += request; ++r41_calls;
        r41_last_receive_us = monotonic_us(); r41_seen_ready = 1;
        pthread_mutex_unlock(&r41_mutex);
        return r41_ok;
    }
    native_epoch = r41_epoch;
    pthread_mutex_unlock(&r41_mutex);
    result = r41_native_receive(handle, buffer, request, received);
    pthread_mutex_lock(&r41_mutex);
    ++r41_calls;
    if (r41_handle != handle || r41_epoch != native_epoch) {
        if (received) *received = 0;
        pthread_mutex_unlock(&r41_mutex);
        return r43_error;
    }
    if (!received || ptr < R41_VIDEO_BUFFER ||
        ptr >= R41_VIDEO_BUFFER + TS_SIZE || !request ||
        request > R41_BLOCK_BYTES ||
        request != R41_BLOCK_BYTES - (ptr - R41_VIDEO_BUFFER)) {
        ++r41_bad_calls;
        pthread_mutex_unlock(&r41_mutex);
        return result;
    }
    offset = (unsigned)(ptr - R41_VIDEO_BUFFER);
    count = *received;
    if ((result != r41_ok && result != r41_incomplete) || count > request ||
        (result == r41_ok && count != request)) {
        if (r41_owned && offset && r41_oem_tail_size == offset)
            memcpy((void *)R41_VIDEO_BUFFER, r41_oem_tail, offset);
        r43_latch_fault(EIO, "native-receive-error");
        r41_oem_tail_size = 0;
        pthread_mutex_unlock(&r41_mutex);
        return result;
    }
    if (!count) {
        pthread_mutex_unlock(&r41_mutex);
        return result;
    }
    r41_native_bytes += count;
    /* Save an independent copy of the OEM trailing packet. Native Most will
     * preserve its own (possibly substituted phone) tail at buffer base.
     */
    total = offset + count;
    tail = total % TS_SIZE;
    {
        uint8_t saved[TS_SIZE];
        unsigned start = total - tail;
        if (tail) {
            unsigned i;
            for (i = 0; i < tail; ++i) {
                unsigned pos = start + i;
                saved[i] = pos < offset ? r41_oem_tail[pos] :
                           ((uint8_t *)buffer)[pos - offset];
            }
        }
        was_owned = r41_owned;
        /* Initial OEM read is NEVER replaced. Reader must subsequently prove
         * it can read a valid full block under the original registered coid.
         */
        r41_last_receive_us = monotonic_us();
        replaced = 0;
        if ((replaced < 0 || (was_owned && !r41_owned)) &&
            offset && r41_oem_tail_size == offset)
            memcpy((void *)R41_VIDEO_BUFFER, r41_oem_tail, offset);
        if (tail) memcpy(r41_oem_tail, saved, tail);
        r41_oem_tail_size = tail;
    }
    /* Accessors yield the ORIGINAL registered fd. One full native block
     * permits starting the borrowed reader, not phone eligibility. Reader
     * must prove a subsequent valid read. Errors latch off for this process.
     */
    if (!r43_faulted && !r43_proxy_handle && !offset && count == R41_BLOCK_BYTES &&
        !tail && cns2_ts_block(buffer, count) &&
        r43_get_ts && r43_get_io && r43_get_fd) {
        void *ts = r43_get_ts(handle);
        void *io = ts ? r43_get_io(ts) : NULL;
        int fd = io ? r43_get_fd(io) : -1;
        int rc = fd < 0 ? EBADF : 0;
        ++r43_start_attempts;
        if (!rc) rc = oem_reader_start(&r43_reader, fd);
        if (!rc) {
            r43_proxy_handle = handle; r43_proxy_fd = fd;
            r43_proxy_epoch = r43_reader.epoch;
            fprintf(stderr, "R43_READER_BORROWED fd=%d epoch=%u verified=0\n", fd, r43_proxy_epoch);
        } else r43_latch_fault(rc, "reader-start-failed");
    }
    pthread_mutex_unlock(&r41_mutex);
    return result;
}

static void r43_destroy_hook(void *handle)
{
    pthread_mutex_lock(&r41_mutex);
    if (r43_proxy_handle == handle || r41_handle == handle) {
        r41_reset_route("native-connection-destroy");
        r41_handle = NULL;
    }
    pthread_mutex_unlock(&r41_mutex);
    r43_native_destroy(handle);
}

#include "intro-state.h"

static void r41_snapshot(void)
{
    /* Snapshot replaces a tiny RAM file; unlike a capped append log it keeps
     * reporting after hours, including phone exit and original output return.
     */
    char text[1536];
    int length;
    unsigned oem_queue;
    unsigned long oem_bytes, oem_discarded;
    FILE *file;
    pthread_mutex_lock(&r41_mutex);
    pthread_mutex_lock(&r43_reader.mutex);
    oem_queue = r43_reader.count; oem_bytes = r43_reader.bytes;
    oem_discarded = r43_reader.discarded;
    pthread_mutex_unlock(&r43_reader.mutex);
    length = snprintf(text, sizeof(text),
        "R43_REGISTERED_MOST_PULL\nhooked=%u ready=%d phone=%u owned=%u returning=%u disabled=%u\n"
        "epoch=%u calls=%lu native_bytes=%lu phone_bytes=%lu\n"
        "frames=%lu keys=%lu queue=%lu spill=%lu acquires=%lu returns=%lu bad_calls=%lu route_resets=%lu\n"
        "last_receive_us=%llu now_us=%llu pts=%u pcr=%u\n"
        "proxy_fd=%d proxy_epoch=%u independent_calls=%lu proxy_errors=%lu\n"
        "OEM_queue=%u OEM_read_bytes=%lu OEM_discard_blocks=%lu supply_gap_max_us=%llu slow_supply_calls=%lu\n"
        "fault_latched=%u fault_error=%d read_result=%ld read_errno=%d start_attempts=%u arm_state=%d wait_polls=%u\n"
        "intro_active=%u intro_preview=%u intro_frames=%u intro_total=%u intro_complete=%u intro_abort=%u intro_seen=%u\n",
        r41_hooked, r41_ready(), r41_phone, r41_owned, r41_returning, r41_disabled,
        r41_epoch, r41_calls, r41_native_bytes, r41_phone_bytes,
        g_frames, g_keyframes, (unsigned long)queue_count(&g_queue),
        (unsigned long)r41_spill_offset, r41_acquires, r41_returns,
        r41_bad_calls, r41_route_resets,
        (unsigned long long)r41_last_receive_us, (unsigned long long)monotonic_us(),
        (unsigned)g_pts_report_90k, (unsigned)g_transport_clock_90k,
        r43_proxy_fd, r43_proxy_epoch, r43_independent_calls, r43_proxy_errors,
        oem_queue, oem_bytes, oem_discarded,
        (unsigned long long)r43_gap_max_us, r43_slow_supply_calls,
        r43_faulted, r43_fault_errno, (long)r43_fault_read_result,
        r43_fault_read_errno, r43_start_attempts, r43_arm_state, r43_arm_wait_polls,
        r433_intro.active, r433_intro.preview, r433_intro.frames, r433_intro.total_frames,
        r433_intro.completed, r433_intro.aborted, r433_intro.seen);
    pthread_mutex_unlock(&r41_mutex);
    if (length <= 0 || length >= (int)sizeof(text)) return;
    file = fopen("/ramdisk/cns2-most-r43.status.part", "w");
    if (!file) return;
    (void)fwrite(text, 1, (size_t)length, file);
    if (!fclose(file))
        (void)rename("/ramdisk/cns2-most-r43.status.part", "/ramdisk/cns2-most-r43.status");
}

static void *r41_consumer(void *unused)
{
    AltFrameRing *ring = MAP_FAILED;
    AvcConfig config;
    uint8_t *message;
    uint32_t generation = 0, next = 1;
    uint64_t next_snapshot = 0, next_disable_check = 0;
    (void)unused;
    memset(&config, 0, sizeof(config));
    message = (uint8_t *)malloc(ALT_FRAME_RING_SLOT_DATA_MAX);
    if (!message) return NULL;
    r433_intro_load();
    for (;;) {
        uint64_t now = monotonic_us();
        int resume;
        if (now >= next_snapshot) {
            r41_snapshot();
            next_snapshot = now + 1000000ULL;
        }
        if (now >= next_disable_check) {
            int disabled = access(R41_DISABLE, F_OK) == 0;
            pthread_mutex_lock(&r41_mutex);
            r41_disabled = (unsigned)disabled;
            if (disabled) r41_request_return("disabled-marker");
            pthread_mutex_unlock(&r41_mutex);
            next_disable_check = now + 250000ULL;
        }
        pthread_mutex_lock(&r41_mutex);
        /* Explicit RAM-only diagnostic request, never an automatic takeover
         * of OEM maps when no phone is connected. Leave pending until ready.
         */
        if (access(R433_PREVIEW_PATH, F_OK) == 0 && !r433_intro.active &&
            r433_intro_begin(1, generation, now)) (void)unlink(R433_PREVIEW_PATH);
        resume = r433_intro_tick(now);
        pthread_mutex_unlock(&r41_mutex);
        if (resume && ring != MAP_FAILED) {
            free_config(&config);
            __sync_synchronize(); next = ring->write_sequence + 1U;
            pthread_mutex_lock(&r41_mutex);
            g_recovery_idr_requested = request_fresh_idr() == 0;
            pthread_mutex_unlock(&r41_mutex);
        }
        if (ring == MAP_FAILED) {
            int fd = open(ALT_FRAME_RING_PATH, O_RDONLY);
            struct stat st;
            if (fd >= 0) {
                if (!fstat(fd, &st) && st.st_size == (off_t)sizeof(*ring))
                    ring = mmap(NULL, sizeof(*ring), PROT_READ, MAP_SHARED, fd, 0);
                close(fd);
            }
            if (ring == MAP_FAILED) { usleep(10000); continue; }
        }
        __sync_synchronize();
        if (ring->magic != ALT_FRAME_RING_MAGIC || ring->version != ALT_FRAME_RING_VERSION) {
            pthread_mutex_lock(&r41_mutex);
            r41_request_return("invalid-ring-header");
            pthread_mutex_unlock(&r41_mutex);
            usleep(1000); continue;
        }
        if (ring->generation != generation) {
            pthread_mutex_lock(&r41_mutex);
            if (!(r433_intro.active && r433_intro.preview)) {
                r41_request_return("ring-generation");
                g_last_sequence = 0;
                g_media_origin_ntp = g_local_origin_us = g_last_pts = 0;
                g_discontinuity_pending = 1;
            }
            pthread_mutex_unlock(&r41_mutex);
            free_config(&config);
            generation = ring->generation;
            next = 1;
        }
        pthread_mutex_lock(&r41_mutex);
        r436_intro_session(ring->session_active != 0);
        if (!ring->session_active) {
            if (!(r433_intro.active && r433_intro.preview)) r41_request_return("phone-disconnect");
            else r41_phone = 0;
        }
        else if (!r41_returning && !r41_disabled) r41_phone = 1;
        int intro_active = r433_intro.active;
        pthread_mutex_unlock(&r41_mutex);
        __sync_synchronize();
        if (intro_active) {
            next = ring->write_sequence + 1U;
            usleep(1000); continue;
        }
        while (next && next <= ring->write_sequence) {
            AltFrameRingSlot *slot = &ring->slots[(next - 1U) % ALT_FRAME_RING_SLOT_COUNT];
            uint32_t commit = slot->committed_sequence;
            uint32_t type, size, flags, source;
            uint64_t timestamp;
            int active;
            if (commit != next) {
                if (commit > next) {
                    pthread_mutex_lock(&r41_mutex);
                    enter_low_latency_recovery(queue_count(&g_queue), commit, monotonic_us());
                    pthread_mutex_unlock(&r41_mutex);
                    next = commit;
                    continue;
                }
                break;
            }
            __sync_synchronize();
            type = slot->type; size = slot->size; flags = slot->flags;
            source = slot->source_sequence;
            timestamp = ((uint64_t)slot->media_timestamp_hi << 32) | slot->media_timestamp_lo;
            if (!size || size > ALT_FRAME_RING_SLOT_DATA_MAX) { ++next; continue; }
            memcpy(message, slot->data, size);
            __sync_synchronize();
            if (slot->committed_sequence != next || ring->generation != generation) break;
            pthread_mutex_lock(&r41_mutex);
            active = ring->session_active && r41_phone && !r41_returning && !r41_disabled;
            if (type == ALT_FRAME_TYPE_CONFIG) {
                if (parse_avcc_config(&config, message, size)) ++g_dropped_messages;
                /* Current producer emits four-byte lengths. This restriction
                 * also bounds AVCC->AnnexB allocation in the reused codec.
                 */
                if (config.valid && config.length_size != 4) {
                    free_config(&config); ++g_dropped_messages;
                }
            } else if (type == ALT_FRAME_TYPE_VIDEO && active && r41_ready()) {
                /* The NAL content, never a metadata flag, proves an IDR. */
                (void)flags;
                int is_idr = -1;
                if (r433_intro.data && !r433_intro.seen && !r41_owned && config.valid) {
                    uint8_t *probe = NULL; size_t probe_size = 0;
                    is_idr = avcc_to_annexb(&config, message, size, 0, &probe, &probe_size);
                    free(probe);
                }
                if (is_idr > 0 && r433_intro_begin(0, generation, monotonic_us())) {
                    (void)r433_intro_tick(monotonic_us());
                    next = ring->write_sequence;
                } else if (process_frame(&config, message, size, 0, source, timestamp) < 0)
                    ++g_dropped_messages;
                if (r43_proxy_handle && g_takeover && queue_count(&g_queue))
                    oem_reader_drop(&r43_reader, 1);
            }
            pthread_mutex_unlock(&r41_mutex);
            ++next;
        }
        /* A static map legitimately sends few frames. Session and native
         * read liveness, not frame silence, determine takeover lifetime.
         */
        usleep(1000);
    }
    return NULL;
}

#ifndef R41_HOST_TEST
static int r41_signatures_match(void)
{
    static const uint8_t receive[16] = {
        0x16,0x21,0xfd,0xeb,0x00,0x30,0xa0,0xe1,0x4c,0x30,0x8d,0xe5,0xf2,0x57,0xff,0xeb
    };
    static const uint8_t transmit[16] = {
        0x04,0xe0,0x2d,0xe5,0x24,0xd0,0x4d,0xe2,0x14,0x00,0x8d,0xe5,0x01,0x30,0xa0,0xe1
    };
    static const uint8_t dma[16] = {
        0x10,0x40,0x2d,0xe9,0x18,0xd0,0x4d,0xe2,0x0c,0x00,0x8d,0xe5,0x01,0x30,0xa0,0xe1
    };
    return !memcmp((void *)0x1bdd30U, receive, sizeof(receive)) &&
           !memcmp((void *)0x1be328U, transmit, sizeof(transmit)) &&
           !memcmp((void *)0x1f7e6cU, dma, sizeof(dma));
}

static void *r41_arm(void *unused)
{
    void *library;
    int *ok, *incomplete, *error;
    r41_receive_fn *slot = (r41_receive_fn *)R41_RECEIVE_GOT;
    pthread_t reader;
    uint64_t next_status = 0;
    (void)unused;
    library = dlopen("/extbin/apps/lib/libTSAPIL.so", RTLD_NOW | RTLD_LOCAL);
    if (!library || !r41_signatures_match()) {
        fprintf(stderr, "R41_REFUSED native-signature-or-library\n"); return NULL;
    }
    r41_native_receive = (r41_receive_fn)dlsym(library, "TSAPIL_Receive");
    ok = (int *)dlsym(library, "TSAPIL_STATUS_OK");
    incomplete = (int *)dlsym(library, "TSAPIL_STATUS_TRANSFER_INCOMPLETE");
    error = (int *)dlsym(library, "TSAPIL_STATUS_ERROR");
    r43_get_ts = (r43_handle_fn)dlsym(library, "TSAPIL_GetTSHandler");
    r43_get_io = (r43_handle_fn)dlsym(library, "TSAPIL_TS_GetFileHandler");
    r43_get_fd = (r43_fd_fn)dlsym(library, "TSAPIL_SAL_IO_GetFD");
    r43_native_destroy = (r43_destroy_fn)dlsym(library, "TSAPIL_DestroyConnectionHandle");
    if (!r41_native_receive || !ok || !incomplete || !error ||
        !r43_get_ts || !r43_get_io || !r43_get_fd || !r43_native_destroy || queue_init(&g_queue)) {
        fprintf(stderr, "R41_REFUSED ABI-or-allocation\n"); return NULL;
    }
    r41_ok = *ok; r41_incomplete = *incomplete; r43_error = *error;
    g_dynamic = &r41_codec_api;
    g_waiting_for_keyframe = g_discontinuity_pending = 1;
    g_latency_wait_start_us = monotonic_us();
    /* Do not overwrite a lazy/unknown relocation or another hook. Wait for
     * original Most to resolve this import while OEM starts its own channel.
     */
    for (;;) {
        r41_receive_fn expected = r41_native_receive;
        int state = r43_import_state((uintptr_t)__atomic_load_n(slot, __ATOMIC_ACQUIRE),
            (uintptr_t)r41_native_receive, 0x001059ccU, (uintptr_t)r41_receive_hook);
        if (access(R41_DISABLE, F_OK) == 0) return NULL;
        pthread_mutex_lock(&r41_mutex);
        r43_arm_state = state; ++r43_arm_wait_polls;
        pthread_mutex_unlock(&r41_mutex);
        if (monotonic_us() >= next_status) {
            r41_snapshot(); next_status = monotonic_us() + 1000000ULL;
        }
        if (state == R43_IMPORT_REFUSE || state == R43_IMPORT_OWNED) {
            fprintf(stderr, "R43_REFUSED competing-or-unknown-receive-import\n"); return NULL;
        }
        if (state == R43_IMPORT_WAIT) { usleep(250000); continue; }
        /* Establish the destructor hook first. No independent fd is retained
         * unless cleanup of the same native connection is installed.
         */
        {
            r43_destroy_fn *destroy_slot = (r43_destroy_fn *)R43_DESTROY_GOT;
            r43_destroy_fn expected_destroy = r43_native_destroy;
            if (*destroy_slot != r43_destroy_hook &&
                !__atomic_compare_exchange_n(destroy_slot, &expected_destroy,
                    r43_destroy_hook, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                /* Saved same-unit Most ELF's verified unbound PLT value.
                 * No calls are made with null handles to force resolution.
                 */
                expected_destroy = (r43_destroy_fn)(uintptr_t)0x001059ccU;
                if (!__atomic_compare_exchange_n(destroy_slot, &expected_destroy,
                    r43_destroy_hook, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    fprintf(stderr, "R43_REFUSED competing-or-unknown-destroy-import\n"); return NULL;
                }
            }
        }
        if (__atomic_compare_exchange_n(slot, &expected, r41_receive_hook, 0,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            pthread_mutex_lock(&r41_mutex);
            r41_hooked = 1;
            pthread_mutex_unlock(&r41_mutex);
            fprintf(stderr, "R41_HOOK_READY got=%p native=%p OK=%d INCOMPLETE=%d\n",
                    (void *)slot, (void *)r41_native_receive, r41_ok, r41_incomplete);
            if (!pthread_create(&reader, NULL, r41_consumer, NULL)) pthread_detach(reader);
            return NULL;
        }
        usleep(250000);
    }
}

__attribute__((visibility("default"))) int cns2_most_r43_probe(void)
{ return 43; }

__attribute__((constructor)) static void r41_constructor(void)
{
    const char *active = getenv(R41_ACTIVE_ENV);
    pthread_t thread;
    if (!active || strcmp(active, "1")) return;
    if (!pthread_create(&thread, NULL, r41_arm, NULL)) pthread_detach(thread);
}
#endif
