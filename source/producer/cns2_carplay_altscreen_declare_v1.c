#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "cns2_variant_parameters.h"
#include "phone-process.h"
#include "video-read.h"

#ifdef CNS2_ALTSCREEN_LIVE_V4
#include "altscreen_frame_protocol.h"
#endif

typedef int32_t OSStatus;
typedef OSStatus (*SessionSetupFn)(void *, const void *, void **);
typedef void (*SessionCommandCompletionFn)(OSStatus, const void *, void *);
typedef OSStatus (*SessionSendCommandFn)(void *, const void *,
                                         SessionCommandCompletionFn, void *);
typedef void *(*CopyServerInfoFn)(void *, const void *, const void *, OSStatus *);
typedef int (*FPrintFFn)(FILE *, const char *, ...);
typedef void *(*CFDictionaryCreateMutableFn)(void *, long, const void *, const void *);
typedef void *(*CFArrayCreateMutableFn)(void *, long, const void *);
typedef void *(*CFArrayCreateMutableCopyFn)(void *, long, const void *);
typedef const void *(*CFDictionaryGetValueFn)(const void *, const void *);
typedef long (*CFArrayGetCountFn)(const void *);
typedef const void *(*CFArrayGetValueAtIndexFn)(const void *, long);
typedef void (*CFArrayAppendValueFn)(void *, const void *);
typedef void (*CFDictionarySetValueFn)(void *, const void *, const void *);
typedef OSStatus (*CFDictionarySetInt64Fn)(void *, const void *, int64_t);
typedef OSStatus (*CFDictionarySetCStringFn)(void *, const void *, const char *, size_t);
typedef void *(*CFStringCreateWithCStringFn)(void *, const char *, uint32_t);
typedef const void *(*CFRetainFn)(const void *);
typedef void (*CFReleaseFn)(const void *);
#ifdef CNS2_ALTSCREEN_AES_V3
typedef void (*DeriveScreenAESFn)(const void *, size_t, uint64_t,
                                  uint8_t[16], uint8_t[16]);
typedef OSStatus (*AESCTRInitFn)(void *, const uint8_t[16],
                                 const uint8_t[16]);
typedef OSStatus (*AESCTRUpdateFn)(void *, const void *, size_t, void *);
typedef void (*AESCTRFinalFn)(void *);
#ifdef CNS2_ALTSCREEN_CHACHA_V5
typedef OSStatus (*PairingSessionDeriveKeyFn)(void *, const void *, size_t,
                                              const void *, size_t, size_t,
                                              void *);
typedef void (*ChaChaInit64x64Fn)(void *, const uint8_t[32],
                                  const uint8_t[8]);
typedef void (*ChaChaAddAADFn)(void *, const void *, size_t);
typedef size_t (*ChaChaDecryptFn)(void *, const void *, size_t, void *);
typedef size_t (*ChaChaVerifyFn)(void *, const void *, const void *, int *);
#endif
#endif

#if defined(CNS2_ALTSCREEN_SHOWUI_V9) && defined(CNS2_ALTSCREEN_PERSISTENT_LOG)
#define LOG_PATH "/ramdisk/.cns2-v43/carplay-maps.log"
#elif defined(CNS2_ALTSCREEN_SHOWUI_V9)
#define LOG_PATH "/tmp/cns2-carplay-altscreen-live-v9-showui.log"
#elif defined(CNS2_ALTSCREEN_REFRESH) && defined(CNS2_ALTSCREEN_PERSISTENT_LOG)
#define LOG_PATH "/persistence/cns275/carplay_maps_live_v8_recovery.log"
#elif defined(CNS2_ALTSCREEN_REFRESH)
#define LOG_PATH "/tmp/cns2-carplay-altscreen-live-v8-recovery.log"
#elif defined(CNS2_ALTSCREEN_PERSISTENT_LOG)
#define LOG_PATH "/persistence/cns275/carplay_maps_live_v7_persistent.log"
#else
#define LOG_PATH "/tmp/cns2-carplay-altscreen-declare-v1.log"
#endif
#define UTF8_ENCODING 0x08000100U
#define ALT_UUID "b7e6c5a0-2222-4000-8000-000000000002"
#define ALT_URL "maps:/car/instrumentcluster/map"
#define CAPTURE_CONFIG_PATH "/tmp/cns2-altscreen-video-config.bin"
#define CAPTURE_HEADER_PATH "/tmp/cns2-altscreen-first-frame-header.bin"
#define CAPTURE_FRAME_PATH "/tmp/cns2-altscreen-first-frame-decrypted.bin"
#ifdef CNS2_ALTSCREEN_REFRESH
#ifndef CNS2_STREAM_RECOVERY_PERIOD
#error Supply CNS2_STREAM_RECOVERY_PERIOD in the local build configuration
#endif
#endif

/*
 * CNS2 0436 libAirPlay.so offsets recovered from the factory live image.
 * Factory/hot-load base: 0x78050000.
 * procman LD_PRELOAD base: 0x78060000 (the preload object shifts AirPlay).
 */
#ifndef CNS2_AIRPLAY_RUNTIME_BASE
#define CNS2_AIRPLAY_RUNTIME_BASE 0x78050000U
#endif
#define SESSION_SETUP_GOT \
    ((uintptr_t *)(uintptr_t)(CNS2_AIRPLAY_RUNTIME_BASE + 0x0003fdc0U))
#define COPY_SERVER_INFO_GOT \
    ((uintptr_t *)(uintptr_t)(CNS2_AIRPLAY_RUNTIME_BASE + 0x00040294U))
#define SESSION_SETUP_REAL \
    ((SessionSetupFn)(uintptr_t)(CNS2_AIRPLAY_RUNTIME_BASE + 0x00027b78U))
#define COPY_SERVER_INFO_REAL \
    ((CopyServerInfoFn)(uintptr_t)(CNS2_AIRPLAY_RUNTIME_BASE + 0x00016910U))
#define AIRPLAY_LAZY_RESOLVER \
    ((uintptr_t)(CNS2_AIRPLAY_RUNTIME_BASE + 0x0000d844U))

static uintptr_t saved_session_setup;
static uintptr_t saved_copy_server_info;
static volatile int armed;
static volatile int in_log;

#ifdef CNS2_ALTSCREEN_PASSIVE_V2
#define SCREEN_HEADER_SIZE 128U
#define SCREEN_MAX_BODY (8U * 1024U * 1024U)
#define SCREEN_OPCODE_VIDEO_FRAME 0U
#define SCREEN_OPCODE_VIDEO_CONFIG 1U
#define ALT_DATA_PORT 7001U

typedef struct PassiveListener {
    int server_fd;
    uint16_t port;
#ifdef CNS2_ALTSCREEN_REFRESH
    void *session;
#endif
#ifdef CNS2_ALTSCREEN_AES_V3
    uint8_t aes_key[16];
    uint8_t aes_iv[16];
    int aes_valid;
#ifdef CNS2_ALTSCREEN_CHACHA_V5
    uint8_t chacha_key[32];
    int chacha_valid;
#endif
#endif
} PassiveListener;

static pthread_mutex_t listener_lock = PTHREAD_MUTEX_INITIALIZER;
static int listener_active;
#endif

static void *sym(const char *name) { return dlsym(RTLD_DEFAULT, name); }

static unsigned long long monotonic_us(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (unsigned long long)ts.tv_sec * 1000000ULL +
           (unsigned long long)ts.tv_nsec / 1000ULL;
}

static void log_object(const char *event, const void *object, OSStatus status)
{
    FILE *file;
    FPrintFFn print_object;
    if (in_log) return;
    in_log = 1;
    file = fopen(LOG_PATH, "a");
    if (!file) { in_log = 0; return; }
    fprintf(file, "t_us=%llu pid=%ld event=%s ptr=%p status=%d",
            monotonic_us(), (long)getpid(), event, object, (int)status);
    print_object = (FPrintFFn)sym("FPrintF");
    if (print_object && object) {
        fputs(" value=", file);
        print_object(file, "%@", object);
    }
    fputc('\n', file);
    fclose(file);
    in_log = 0;
}

static void *make_string(const char *value)
{
    CFStringCreateWithCStringFn fn =
        (CFStringCreateWithCStringFn)sym("CFStringCreateWithCString");
    return fn ? fn(NULL, value, UTF8_ENCODING) : NULL;
}

static void release_cf(const void *object)
{
    CFReleaseFn fn = (CFReleaseFn)sym("CFRelease");
    if (fn && object) fn(object);
}

#ifdef CNS2_ALTSCREEN_REFRESH
static const void *retain_cf(const void *object)
{
    CFRetainFn fn = (CFRetainFn)sym("CFRetain");
    return fn && object ? fn(object) : NULL;
}
#endif

static void *create_dictionary(void)
{
    CFDictionaryCreateMutableFn fn =
        (CFDictionaryCreateMutableFn)sym("CFDictionaryCreateMutable");
    const void *keys = sym("kCFLDictionaryKeyCallBacksCFLTypes");
    const void *values = sym("kCFLDictionaryValueCallBacksCFLTypes");
    return fn && keys && values ? fn(NULL, 0, keys, values) : NULL;
}

static void *create_array(void)
{
    CFArrayCreateMutableFn fn =
        (CFArrayCreateMutableFn)sym("CFArrayCreateMutable");
    const void *callbacks = sym("kCFLArrayCallBacksCFLTypes");
    return fn && callbacks ? fn(NULL, 0, callbacks) : NULL;
}

static OSStatus set_object(void *dict, const char *name, const void *value)
{
    CFDictionarySetValueFn fn =
        (CFDictionarySetValueFn)sym("CFDictionarySetValue");
    void *key = make_string(name);
    OSStatus status = -1;
    if (fn && key && value) { fn(dict, key, value); status = 0; }
    release_cf(key);
    return status;
}

#if CNS2_SAFE_X != CNS2_NAV_X
static OSStatus set_adjacent(void *dict,int index)
{
    typedef void *(*NumberFn)(int64_t);
    NumberFn make=(NumberFn)sym("CFNumberCreateInt64");
    CFArrayAppendValueFn append=(CFArrayAppendValueFn)sym("CFArrayAppendValue");
    void *array=create_array(),*number=make?make(index):NULL;
    OSStatus status=array&&number&&append?0:-1;
    if(!status){append(array,number);status=set_object(dict,"adjacentViewAreas",array);}
    release_cf(number);release_cf(array);return status;
}
#endif

static OSStatus set_int(void *dict, const char *name, int64_t value)
{
    CFDictionarySetInt64Fn fn =
        (CFDictionarySetInt64Fn)sym("CFDictionarySetInt64");
    void *key = make_string(name);
    OSStatus status = fn && key ? fn(dict, key, value) : -1;
    release_cf(key);
    return status;
}

static OSStatus set_cstring(void *dict, const char *name, const char *value)
{
    CFDictionarySetCStringFn fn =
        (CFDictionarySetCStringFn)sym("CFDictionarySetCString");
    void *key = make_string(name);
    OSStatus status = fn && key ? fn(dict, key, value, (size_t)-1) : -1;
    release_cf(key);
    return status;
}

static const void *get_object(const void *dict, const char *name)
{
    CFDictionaryGetValueFn fn =
        (CFDictionaryGetValueFn)sym("CFDictionaryGetValue");
    void *key = make_string(name);
    const void *value = fn && key ? fn(dict, key) : NULL;
    release_cf(key);
    return value;
}

#ifdef CNS2_ALTSCREEN_REFRESH
typedef struct ForceKeyFrameContext {
    void *session;
} ForceKeyFrameContext;

static void force_keyframe_completion(OSStatus status, const void *response,
                                      void *context)
{
    ForceKeyFrameContext *command = (ForceKeyFrameContext *)context;
    log_object("v8-force-keyframe-completion", response, status);
    if (command) {
        release_cf(command->session);
        free(command);
    }
}

static OSStatus send_force_keyframe(void *session)
{
    SessionSendCommandFn send_command = (SessionSendCommandFn)sym(
        "AirPlayReceiverSessionSendCommand");
    void *request = create_dictionary();
    void *params = create_dictionary();
    ForceKeyFrameContext *context = NULL;
    OSStatus status = send_command && request && params && session ? 0 : -1;

    if (!status) status = set_cstring(request, "type", "forceKeyFrame");
    if (!status) status = set_cstring(params, "uuid", ALT_UUID);
    if (!status) status = set_object(request, "params", params);
    if (!status) {
        context = (ForceKeyFrameContext *)calloc(1, sizeof(*context));
        if (!context) status = -1;
    }
    if (!status) {
        context->session = (void *)retain_cf(session);
        if (!context->session) status = -1;
    }
    if (!status) {
        status = send_command(session, request, force_keyframe_completion,
                              context);
        if (!status) context = NULL;
    }
    if (context) {
        release_cf(context->session);
        free(context);
    }
    release_cf(params);
    release_cf(request);
    return status;
}

#ifdef CNS2_SKODA_ROLLER_CONTROL
#include "roller-control.h"
#endif

/* Protocol-only port of the reference view-area request. No MHI2 offsets,
 * transports, session globals, extra workers, or native-file edits are used. */
#if CNS2_SAFE_X != CNS2_NAV_X
typedef struct ViewState {
    int candidate, sent, attempts;
    uint64_t candidate_since, last_poll;
} ViewState;
static void log_values(const char *,int,int,unsigned long long);

static uint64_t view_ms(void)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now))return 0;
    return (uint64_t)now.tv_sec*1000U+(uint64_t)now.tv_nsec/1000000U;
}
static OSStatus send_view_update(void *session,int index)
{
    SessionSendCommandFn send=(SessionSendCommandFn)sym("AirPlayReceiverSessionSendCommand");
    void *request=create_dictionary(),*params=create_dictionary();
    ForceKeyFrameContext *context=NULL;
    OSStatus status=send&&session&&request&&params?0:-1;
    if(!status)status=set_cstring(request,"type","updateViewArea");
    if(!status)status=set_cstring(params,"uuid",ALT_UUID);
    if(!status)status=set_int(params,"viewAreaIndex",index);
    if(!status)status=set_int(params,"animationDurationMillis",0);
    if(!status)status=set_adjacent(params,1-index);
    if(!status)status=set_object(request,"params",params);
    if(!status){
        context=calloc(1,sizeof(*context));
        if(!context)status=-1;
    }
    if(!status){
        context->session=(void*)retain_cf(session);
        if(!context->session)status=-1;
    }
    if(!status){
        status=send(session,request,force_keyframe_completion,context);
        if(!status)context=NULL;
    }
    if(context){release_cf(context->session);free(context);}
    release_cf(params);release_cf(request);
    log_values("v43-viewarea-update",index,status,0);
    return status;
}
static void poll_view_state(void *session,ViewState *state)
{
    uint64_t now=view_ms();
    FILE *f;char body[2048],*marker,*end;size_t n;long value;int target;
    if(now<state->last_poll+250U)return;
    state->last_poll=now;
    f=fopen("/ramdisk/cns275-carplayvc/v531-rgd-cache.pps","r");
    if(!f)return;
    n=fread(body,1,sizeof(body)-1,f);fclose(f);
    if(!n||n>=sizeof(body)-1)return;
    body[n]=0;marker=strstr(body,"state::");if(!marker)return;
    errno=0;value=strtol(marker+7,&end,10);
    if(errno||end==marker+7||(*end&&*end!='\n'&&*end!='\r')||value<0||value>255)return;
    target=value>0?1:0;
    if(target!=state->candidate){
        state->candidate=target;state->candidate_since=now;state->attempts=0;return;
    }
    if(target!=state->sent&&now>=state->candidate_since+400U&&state->attempts<3){
        ++state->attempts;
        if(!send_view_update(session,target))state->sent=target;
    }
}
#endif

#ifdef CNS2_ALTSCREEN_SHOWUI_V9
typedef struct ShowUIContext {
    void *session;
} ShowUIContext;

static void *force_after_showui_thread(void *opaque)
{
    ShowUIContext *context = (ShowUIContext *)opaque;
    OSStatus status = context && context->session ?
        send_force_keyframe(context->session) : -1;
    log_object("v9-force-after-showui", context ? context->session : NULL,
               status);
    if (context) {
        release_cf(context->session);
        free(context);
    }
    return NULL;
}

static void showui_completion(OSStatus status, const void *response,
                              void *opaque)
{
    ShowUIContext *context = (ShowUIContext *)opaque;
    log_object("v9-showui-completion", response, status);
    if (status == 0 && context && context->session) {
        ShowUIContext *force_context =
            (ShowUIContext *)calloc(1, sizeof(*force_context));
        pthread_t thread;
        int thread_status = -1;
        if (force_context) {
            force_context->session = (void *)retain_cf(context->session);
            if (force_context->session) {
                thread_status = pthread_create(&thread, NULL,
                                               force_after_showui_thread,
                                               force_context);
            }
        }
        if (thread_status == 0) {
            pthread_detach(thread);
        } else {
            if (force_context) {
                release_cf(force_context->session);
                free(force_context);
            }
            log_object("v9-force-after-showui-schedule-failed",
                       context->session, thread_status);
        }
    }
    if (context) {
        release_cf(context->session);
        free(context);
    }
}

static OSStatus send_showui(void *session)
{
    SessionSendCommandFn send_command = (SessionSendCommandFn)sym(
        "AirPlayReceiverSessionSendCommand");
    void *request = create_dictionary();
    void *params = create_dictionary();
    ShowUIContext *context = NULL;
    OSStatus status = send_command && request && params && session ? 0 : -1;

    if (!status) status = set_cstring(request, "type", "showUI");
    if (!status) status = set_cstring(params, "uuid", ALT_UUID);
    if (!status) status = set_cstring(params, "url", ALT_URL);
    if (!status) status = set_object(request, "params", params);
    if (!status) {
        context = (ShowUIContext *)calloc(1, sizeof(*context));
        if (!context) status = -1;
    }
    if (!status) {
        context->session = (void *)retain_cf(session);
        if (!context->session) status = -1;
    }
    if (!status) {
        log_object("v9-showui-request", request, 0);
        status = send_command(session, request, showui_completion, context);
        if (!status) context = NULL;
    }
    if (context) {
        release_cf(context->session);
        free(context);
    }
    release_cf(params);
    release_cf(request);
    return status;
}

static void *delayed_showui_thread(void *opaque)
{
    ShowUIContext *context = (ShowUIContext *)opaque;
    OSStatus status;
    usleep(1000U * 1000U);
    status = context && context->session ? send_showui(context->session) : -1;
    log_object("v9-showui-dispatch", context ? context->session : NULL,
               status);
    if (context) {
        release_cf(context->session);
        free(context);
    }
    return NULL;
}

static OSStatus schedule_showui(void *session)
{
    ShowUIContext *context = (ShowUIContext *)calloc(1, sizeof(*context));
    pthread_t thread;
    int status;
    if (!context) return -1;
    context->session = (void *)retain_cf(session);
    if (!context->session) {
        free(context);
        return -1;
    }
    status = pthread_create(&thread, NULL, delayed_showui_thread, context);
    if (status != 0) {
        release_cf(context->session);
        free(context);
        return status;
    }
    pthread_detach(thread);
    return 0;
}
#endif

static int consume_force_keyframe_marker(void)
{
    return unlink(ALT_FRAME_FORCE_IDR_PATH) == 0;
}
#endif

#ifdef CNS2_ALTSCREEN_PASSIVE_V2
static void log_values(const char *event, int value1, int value2,
                       unsigned long long value3)
{
    FILE *file = fopen(LOG_PATH, "a");
    if (!file) return;
    fprintf(file,
            "t_us=%llu pid=%ld event=%s value1=%d value2=%d value3=%llu\n",
            monotonic_us(), (long)getpid(), event, value1, value2, value3);
    fclose(file);
}

static int write_capture_once(const char *path, const void *data, size_t size)
{
    FILE *file;
    size_t written;
    if (!path || !data || size == 0 || access(path, F_OK) == 0) return 1;
    file = fopen(path, "wb");
    if (!file) return -1;
    written = fwrite(data, 1, size, file);
    if (fclose(file) != 0 || written != size) {
        unlink(path);
        return -1;
    }
    return 0;
}

static int wait_readable(int fd, int seconds)
{
    fd_set set;
    struct timeval timeout;
    int result;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    do {
        result = select(fd + 1, &set, NULL, NULL, &timeout);
    } while (result < 0 && errno == EINTR);
    return result;
}

#include "video-control.h"

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#ifdef CNS2_ALTSCREEN_AES_V3
typedef union AESCTRStorage {
    uint64_t align64;
    void *align_ptr;
    uint8_t bytes[512];
} AESCTRStorage;

static int inspect_avcc(const uint8_t *frame, size_t size,
                        int *out_first_type, int *out_has_idr)
{
    size_t offset = 0;
    int count = 0, first_type = -1, has_idr = 0;
    while (offset + 4 <= size) {
        uint32_t nal_size = ((uint32_t)frame[offset] << 24) |
                            ((uint32_t)frame[offset + 1] << 16) |
                            ((uint32_t)frame[offset + 2] << 8) |
                            (uint32_t)frame[offset + 3];
        offset += 4;
        if (nal_size == 0 || nal_size > size - offset) return 0;
        if (first_type < 0) first_type = frame[offset] & 0x1f;
        if ((frame[offset] & 0x1f) == 5) has_idr = 1;
        offset += nal_size;
        ++count;
    }
    if (out_first_type) *out_first_type = first_type;
    if (out_has_idr) *out_has_idr = has_idr;
    return offset == size ? count : 0;
}

#ifdef CNS2_ALTSCREEN_LIVE_V4
static AltFrameRing *create_forward_ring(void)
{
    AltFrameRing *ring;
    uint32_t generation = 1;
    unsigned int i;
    int fd = open(ALT_FRAME_RING_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) return NULL;
    if (ftruncate(fd, (off_t)sizeof(*ring)) != 0) {
        close(fd);
        return NULL;
    }
    ring = (AltFrameRing *)mmap(NULL, sizeof(*ring), PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, 0);
    close(fd);
    if (ring == MAP_FAILED) return NULL;
    if (ring->magic == ALT_FRAME_RING_MAGIC &&
        ring->version == ALT_FRAME_RING_VERSION)
        generation = ring->generation + 1U;
    ring->magic = 0;
    __sync_synchronize();
    ring->version = ALT_FRAME_RING_VERSION;
    ring->generation = generation ? generation : 1U;
    ring->write_sequence = 0;
    ring->session_active = 0;
    ring->reserved = 0;
    for (i = 0; i < ALT_FRAME_RING_SLOT_COUNT; ++i)
        ring->slots[i].committed_sequence = 0;
    __sync_synchronize();
    ring->magic = ALT_FRAME_RING_MAGIC;
    return ring;
}

static int forward_ring_message(AltFrameRing *ring, uint16_t type,
                                uint32_t source_sequence, uint32_t flags,
                                uint64_t media_timestamp_ns,
                                const uint8_t *data, size_t size)
{
    AltFrameRingSlot *slot;
    uint32_t sequence;
    if (!ring || !data || size == 0 || size > ALT_FRAME_RING_SLOT_DATA_MAX)
        return -1;
    sequence = ring->write_sequence + 1U;
    if (sequence == 0) sequence = 1U;
    slot = &ring->slots[(sequence - 1U) % ALT_FRAME_RING_SLOT_COUNT];
    slot->committed_sequence = 0;
    __sync_synchronize();
    slot->type = type;
    slot->size = (uint32_t)size;
    slot->flags = flags;
    slot->source_sequence = source_sequence;
    slot->media_timestamp_lo = (uint32_t)media_timestamp_ns;
    slot->media_timestamp_hi = (uint32_t)(media_timestamp_ns >> 32);
    memcpy(slot->data, data, size);
    __sync_synchronize();
    slot->committed_sequence = sequence;
    __sync_synchronize();
    ring->write_sequence = sequence;
    return 1;
}
#endif
#endif

static void *passive_listener_thread(void *context)
{
    PassiveListener *listener = (PassiveListener *)context;
    uint8_t header[SCREEN_HEADER_SIZE];
    uint8_t *body = NULL;
    size_t capacity = 0;
    unsigned long long messages = 0, frames = 0;
    int client = -1;
    int one = 1;
    Cns2VideoControl video_control={0};
    Cns2VideoRead video_io={&video_control,cns2_video_now,cns2_video_pump,
                           cns2_video_wait,cns2_video_receive,3000000ULL};
#if CNS2_SAFE_X != CNS2_NAV_X
    ViewState view_state={-1,0,0,0,0};
#endif
#ifdef CNS2_ALTSCREEN_AES_V3
    int captured_header = 0;
#ifdef CNS2_ALTSCREEN_LIVE_V4
    AltFrameRing *forward_ring = NULL;
    uint8_t *cached_config = NULL;
    size_t cached_config_size = 0;
    unsigned long long forwarded_frames = 0;
#endif
#endif
#ifdef CNS2_ALTSCREEN_AES_V3
    AESCTRStorage aes_storage;
    AESCTRInitFn aes_init = (AESCTRInitFn)sym("AES_CTR_Init");
    AESCTRUpdateFn aes_update = (AESCTRUpdateFn)sym("AES_CTR_Update");
    AESCTRFinalFn aes_final = (AESCTRFinalFn)sym("AES_CTR_Final");
    int aes_ready = 0;
#ifdef CNS2_ALTSCREEN_CHACHA_V5
    AESCTRStorage chacha_storage;
    ChaChaInit64x64Fn chacha_init = (ChaChaInit64x64Fn)sym(
        "chacha20_poly1305_init_64x64");
    ChaChaAddAADFn chacha_add_aad = (ChaChaAddAADFn)sym(
        "chacha20_poly1305_add_aad");
    ChaChaDecryptFn chacha_decrypt = (ChaChaDecryptFn)sym(
        "chacha20_poly1305_decrypt");
    ChaChaVerifyFn chacha_verify = (ChaChaVerifyFn)sym(
        "chacha20_poly1305_verify");
    uint8_t chacha_counter[8];
    int chacha_ready = 0;
#endif

    memset(&aes_storage, 0, sizeof(aes_storage));
#ifdef CNS2_ALTSCREEN_CHACHA_V5
    memset(&chacha_storage, 0, sizeof(chacha_storage));
    memset(chacha_counter, 0, sizeof(chacha_counter));
    if (listener->chacha_valid && chacha_init && chacha_add_aad &&
        chacha_decrypt && chacha_verify) {
        chacha_ready = 1;
        log_values("v5-chacha-ready", listener->port, 32, 0);
    } else if (listener->chacha_valid) {
        log_values("v5-chacha-unavailable", listener->chacha_valid,
                   chacha_init && chacha_add_aad && chacha_decrypt &&
                       chacha_verify,
                   0);
    }
#endif
#ifdef CNS2_ALTSCREEN_CHACHA_V5
    if (!listener->chacha_valid && listener->aes_valid &&
#else
    if (listener->aes_valid &&
#endif
        aes_init && aes_update && aes_final &&
        aes_init(aes_storage.bytes, listener->aes_key, listener->aes_iv) == 0) {
        aes_ready = 1;
        log_values("v3-aes-ctr-ready", listener->port, 16, 0);
#ifdef CNS2_ALTSCREEN_CHACHA_V5
    } else if (!listener->chacha_valid) {
#else
    } else {
#endif
        log_values("v3-aes-ctr-unavailable", listener->aes_valid,
                   aes_init && aes_update && aes_final, 0);
    }
#ifdef CNS2_ALTSCREEN_LIVE_V4
    forward_ring = create_forward_ring();
    log_values("v4-forward-ring-ready", forward_ring != NULL,
               (int)ALT_FRAME_RING_SLOT_COUNT, sizeof(AltFrameRing));
#endif
#endif

    log_values("v2-listener-waiting", listener->port,
               listener->server_fd, 0);
    if (wait_readable(listener->server_fd, 90) > 0)
        client = accept(listener->server_fd, NULL, NULL);
    close(listener->server_fd);
    if (client < 0) {
        log_values("v2-listener-accept-failed", listener->port, errno, 0);
        goto done;
    }
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef CNS2_ALTSCREEN_LIVE_V4
    if (forward_ring) {
        forward_ring->session_active = 1;
        __sync_synchronize();
    }
#endif
    log_values("v2-video-connected", listener->port, client, 0);
    video_control.session=listener->session;
    video_control.phone_pid=cns2_find_usb_phone();
    log_values("v4341-lifecycle-bound",(int)video_control.phone_pid,100,3000);
#ifdef CNS2_SKODA_ROLLER_CONTROL
    roller_activate(listener->session);
#endif
    for (;;) {
        uint32_t body_size;
        uint8_t opcode;
        uint64_t media_timestamp_ns = 0;
#if CNS2_SAFE_X != CNS2_NAV_X
        poll_view_state(listener->session,&view_state);
#endif
        int read_status = cns2_video_read_exact(&video_io,client,header,sizeof(header),0);
        if (read_status != 0) {
#ifdef CNS2_ALTSCREEN_LIFECYCLE_V7
            if (read_status == -2)
                log_values("v4341-partial-header-timeout",1,3000,frames);
#endif
            break;
        }
        body_size = read_u32_le(header);
        opcode = header[4];
        memcpy(&media_timestamp_ns, header + 8, sizeof(media_timestamp_ns));
        if (body_size > SCREEN_MAX_BODY) {
            log_values("v2-invalid-body", (int)body_size, opcode, messages);
            break;
        }
        if (body_size > capacity) {
            uint8_t *grown = (uint8_t *)realloc(body, body_size ? body_size : 1);
            if (!grown) break;
            body = grown;
            capacity = body_size;
        }
        if (body_size) {
            read_status = cns2_video_read_exact(&video_io,client,body,body_size,1);
            if (read_status != 0) {
#ifdef CNS2_ALTSCREEN_LIFECYCLE_V7
                if (read_status == -2)
                    log_values("v4341-partial-body-timeout",2,3000,frames);
#endif
                break;
            }
        }
        ++messages;
        if (opcode == SCREEN_OPCODE_VIDEO_CONFIG) {
            float width = 0.0f, height = 0.0f;
            memcpy(&width, header + 16, sizeof(width));
            memcpy(&height, header + 20, sizeof(height));
            log_values("v2-video-config", (int)width, (int)height, body_size);
#ifdef CNS2_ALTSCREEN_AES_V3
            if (body_size > 0) {
                int capture_status = write_capture_once(
                    CAPTURE_CONFIG_PATH, body, body_size);
                log_values("v3-config-capture", capture_status,
                           (int)body_size, messages);
            }
#ifdef CNS2_ALTSCREEN_LIVE_V4
            free(cached_config);
            cached_config = NULL;
            cached_config_size = 0;
            if (body_size > 0) {
                cached_config = (uint8_t *)malloc(body_size);
                if (cached_config) {
                    memcpy(cached_config, body, body_size);
                    cached_config_size = body_size;
                }
                log_values("v4-forward-config",
                           forward_ring_message(forward_ring,
                               ALT_FRAME_TYPE_CONFIG, 0, 0,
                               media_timestamp_ns, body, body_size),
                           (int)body_size, messages);
            }
#endif
#endif
        } else if (opcode == SCREEN_OPCODE_VIDEO_FRAME) {
            ++frames;
#ifdef CNS2_ALTSCREEN_AES_V3
            if (!captured_header) {
                int capture_status = write_capture_once(
                    CAPTURE_HEADER_PATH, header, sizeof(header));
                log_values("v3-header-capture", capture_status,
                           (int)sizeof(header), frames);
                captured_header = 1;
            }
            if (body_size > 0) {
                int first_type = -1, has_idr = 0;
                int nal_count;
                size_t plain_size = body_size;
                int decrypted = 0;
#ifdef CNS2_ALTSCREEN_CHACHA_V5
                if (chacha_ready && body_size > 16U) {
                    size_t cipher_size = body_size - 16U;
                    size_t processed, tail = 0;
                    int auth_status = -1;
                    unsigned int counter_index;
                    memset(&chacha_storage, 0, sizeof(chacha_storage));
                    chacha_init(chacha_storage.bytes, listener->chacha_key,
                                chacha_counter);
                    chacha_add_aad(chacha_storage.bytes, header,
                                   sizeof(header));
                    processed = chacha_decrypt(chacha_storage.bytes, body,
                                               cipher_size, body);
                    if (processed <= cipher_size)
                        tail = chacha_verify(chacha_storage.bytes,
                                             body + processed,
                                             body + cipher_size,
                                             &auth_status);
                    if (auth_status == 0 && processed + tail == cipher_size) {
                        plain_size = cipher_size;
                        decrypted = 1;
                        for (counter_index = 0;
                             counter_index < sizeof(chacha_counter);
                             ++counter_index) {
                            if (++chacha_counter[counter_index] != 0) break;
                        }
                        if (frames == 1 || (frames % 30ULL) == 0)
                            log_values("v5-chacha-auth-ok", (int)plain_size,
                                       (int)(processed + tail), frames);
                    } else if (frames == 1 || (frames % 30ULL) == 0) {
                        log_values("v5-chacha-auth-failed", auth_status,
                                   (int)(processed + tail), frames);
                    }
                } else
#endif
                if (aes_ready) {
                    OSStatus decrypt_status = aes_update(
                        aes_storage.bytes, body, body_size, body);
                    if (decrypt_status != 0) {
                        log_values("v3-aes-decrypt-failed", decrypt_status,
                                   (int)body_size, frames);
                        aes_ready = 0;
                    } else {
                        decrypted = 1;
                    }
                }
                if (decrypted) {
                    nal_count = inspect_avcc(body, plain_size,
                                             &first_type, &has_idr);
                    if (frames == 1 || (frames % 30ULL) == 0 || has_idr)
                        log_values(nal_count > 0 ? "v3-avcc-valid" :
                                   "v3-avcc-invalid", (int)plain_size,
                                   first_type, frames);
                    if (nal_count > 0) {
                        int capture_status = write_capture_once(
                            CAPTURE_FRAME_PATH, body, plain_size);
                        if (capture_status <= 0)
                            log_values("v3-frame-capture", capture_status,
                                       (int)plain_size, frames);
                    }
                    if (nal_count > 0 && has_idr)
                        log_values("v3-idr", (int)plain_size,
                                   nal_count, frames);
                    if(nal_count>0&&has_idr)video_control.idr_attempts=0;
#ifdef CNS2_ALTSCREEN_LIVE_V4
                    if (nal_count > 0 && forward_ring) {
                        int forward_status;
                        if ((has_idr || (frames % 30ULL) == 1) &&
                            cached_config && cached_config_size > 0)
                            forward_ring_message(forward_ring,
                                ALT_FRAME_TYPE_CONFIG, (uint32_t)frames, 0,
                                media_timestamp_ns, cached_config,
                                cached_config_size);
                        forward_status = forward_ring_message(forward_ring,
                            ALT_FRAME_TYPE_VIDEO, (uint32_t)frames,
                            has_idr ? ALT_FRAME_FLAG_KEYFRAME : 0,
                            media_timestamp_ns, body, plain_size);
                        if (forward_status > 0) ++forwarded_frames;
                        if (frames == 1 || (frames % 30ULL) == 0 || has_idr)
                            log_values(forward_status > 0 ?
                                "v4-forward-frame" : "v4-forward-drop",
                                forward_status, (int)plain_size, frames);
                    }
#endif
#ifdef CNS2_ALTSCREEN_REFRESH
                    if (nal_count > 0 && listener->session) {
                        if ((frames % CNS2_STREAM_RECOVERY_PERIOD) == 0) {
                            OSStatus force_status = send_force_keyframe(
                                listener->session);
                            log_values("v8-periodic-force-keyframe",
                                force_status,
                                (int)CNS2_STREAM_RECOVERY_PERIOD, frames);
                        }
                    }
#endif
                }
            }
#endif
            if (frames == 1 || (frames % 30ULL) == 0)
                log_values("v2-video-frame-encrypted", (int)body_size,
                           opcode, frames);
        } else {
            log_values("v2-screen-message", (int)body_size, opcode, messages);
        }
    }
    log_values("v2-video-disconnected", listener->port, errno, frames);

done:
#ifdef CNS2_SKODA_ROLLER_CONTROL
    roller_deactivate(listener->session);
#endif
#ifdef CNS2_ALTSCREEN_LIVE_V4
    if (forward_ring) {
        forward_ring->session_active = 0;
        __sync_synchronize();
    }
#endif
    if (client >= 0) close(client);
#ifdef CNS2_ALTSCREEN_AES_V3
    if (aes_ready && aes_final) aes_final(aes_storage.bytes);
#ifdef CNS2_ALTSCREEN_LIVE_V4
    if (forward_ring && forward_ring != MAP_FAILED)
        munmap(forward_ring, sizeof(*forward_ring));
    free(cached_config);
    log_values("v4-forward-summary", (int)forwarded_frames,
               (int)frames, messages);
#endif
#endif
    free(body);
#ifdef CNS2_ALTSCREEN_REFRESH
    release_cf(listener->session);
#endif
    free(listener);
    pthread_mutex_lock(&listener_lock);
    listener_active = 0;
    pthread_mutex_unlock(&listener_lock);
    return NULL;
}

static OSStatus start_passive_listener(uint16_t *out_port
#ifdef CNS2_ALTSCREEN_AES_V3
                                       , const uint8_t aes_key[16],
                                       const uint8_t aes_iv[16], int aes_valid
#ifdef CNS2_ALTSCREEN_CHACHA_V5
                                       , const uint8_t chacha_key[32],
                                       int chacha_valid
#endif
#endif
#ifdef CNS2_ALTSCREEN_REFRESH
                                       , void *session
#endif
                                       )
{
    struct sockaddr_in6 address;
    PassiveListener *listener = NULL;
    pthread_t thread;
    int fd = -1, one = 1, status = -1;

    pthread_mutex_lock(&listener_lock);
    if (listener_active) {
        *out_port = ALT_DATA_PORT;
        pthread_mutex_unlock(&listener_lock);
        return 1;
    }
    fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) goto done;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
    memset(&address, 0, sizeof(address));
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any;
    address.sin6_port = htons(ALT_DATA_PORT);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(fd, 1) != 0) goto done;
    listener = (PassiveListener *)calloc(1, sizeof(*listener));
    if (!listener) goto done;
    listener->server_fd = fd;
    listener->port = ALT_DATA_PORT;
#ifdef CNS2_ALTSCREEN_REFRESH
    listener->session = (void *)retain_cf(session);
    if (!listener->session) goto done;
#endif
#ifdef CNS2_ALTSCREEN_AES_V3
    if (aes_valid) {
        memcpy(listener->aes_key, aes_key, sizeof(listener->aes_key));
        memcpy(listener->aes_iv, aes_iv, sizeof(listener->aes_iv));
        listener->aes_valid = 1;
    }
#ifdef CNS2_ALTSCREEN_CHACHA_V5
    if (chacha_valid) {
        memcpy(listener->chacha_key, chacha_key,
               sizeof(listener->chacha_key));
        listener->chacha_valid = 1;
    }
#endif
#endif
    if (pthread_create(&thread, NULL, passive_listener_thread, listener) != 0)
        goto done;
    pthread_detach(thread);
    listener_active = 1;
    *out_port = ALT_DATA_PORT;
    fd = -1;
    listener = NULL;
    status = 0;

done:
    if (fd >= 0) close(fd);
#ifdef CNS2_ALTSCREEN_REFRESH
    if (listener) release_cf(listener->session);
#endif
    free(listener);
    pthread_mutex_unlock(&listener_lock);
    return status;
}

static int first_stream_type(const void *request)
{
    CFArrayGetCountFn count_fn =
        (CFArrayGetCountFn)sym("CFArrayGetCount");
    CFArrayGetValueAtIndexFn value_fn =
        (CFArrayGetValueAtIndexFn)sym("CFArrayGetValueAtIndex");
    const void *streams = get_object(request, "streams");
    const void *stream;
    void *key;
    OSStatus status = -1;
    int64_t type = -1;
    typedef int64_t (*GetIntFn)(const void *, const void *, OSStatus *);
    GetIntFn get_int = (GetIntFn)sym("CFDictionaryGetInt64");
    if (!streams || !count_fn || !value_fn || count_fn(streams) < 1 ||
        !get_int) return -1;
    stream = value_fn(streams, 0);
    key = make_string("type");
    if (stream && key) type = get_int(stream, key, &status);
    release_cf(key);
    return status == 0 ? (int)type : -1;
}

#ifdef CNS2_ALTSCREEN_AES_V3
static uint64_t first_stream_connection_id(const void *request,
                                           OSStatus *out_status)
{
    CFArrayGetCountFn count_fn =
        (CFArrayGetCountFn)sym("CFArrayGetCount");
    CFArrayGetValueAtIndexFn value_fn =
        (CFArrayGetValueAtIndexFn)sym("CFArrayGetValueAtIndex");
    const void *streams = get_object(request, "streams");
    const void *stream;
    void *key;
    OSStatus status = -1;
    int64_t value = 0;
    typedef int64_t (*GetIntFn)(const void *, const void *, OSStatus *);
    GetIntFn get_int = (GetIntFn)sym("CFDictionaryGetInt64");
    if (!streams || !count_fn || !value_fn || count_fn(streams) < 1 ||
        !get_int) goto done;
    stream = value_fn(streams, 0);
    key = make_string("streamConnectionID");
    if (stream && key) value = get_int(stream, key, &status);
    release_cf(key);
done:
    if (out_status) *out_status = status;
    return (uint64_t)value;
}

#ifdef CNS2_ALTSCREEN_CHACHA_V5
static OSStatus derive_screen_chacha_key(void *session, uint64_t connection_id,
                                        uint8_t out_key[32])
{
    static const char info[] = "DataStream-Output-Encryption-Key";
    PairingSessionDeriveKeyFn derive =
        (PairingSessionDeriveKeyFn)sym("PairingSessionDeriveKey");
    void *pairing_session = NULL;
    char salt[64];
    int salt_length;
    if (!session || !out_key || !derive) return -1;
    memcpy(&pairing_session, (const uint8_t *)session + 0x3DCU,
           sizeof(pairing_session));
    if (!pairing_session) return -1;
    salt_length = snprintf(salt, sizeof(salt), "DataStream-Salt%llu",
                           (unsigned long long)connection_id);
    if (salt_length <= 0 || (size_t)salt_length >= sizeof(salt)) return -1;
    return derive(pairing_session, salt, (size_t)salt_length,
                  info, sizeof(info) - 1U, 32U, out_key);
}
#endif
#endif

static OSStatus set_alt_stream_response(void *response, uint16_t port)
{
    CFArrayAppendValueFn append =
        (CFArrayAppendValueFn)sym("CFArrayAppendValue");
    void *streams = create_array();
    void *stream = create_dictionary();
    OSStatus status = streams && stream && append ? 0 : -1;
    if (!status) status = set_int(stream, "type", 111);
    if (!status) status = set_int(stream, "dataPort", port);
    if (!status) append(streams, stream);
    if (!status) status = set_object(response, "streams", streams);
    release_cf(stream);
    release_cf(streams);
    return status;
}
#endif

static void *create_area(int x, int y, int width, int height)
{
    void *area = create_dictionary();
    OSStatus status = area ? 0 : -1;
    if (!status) status = set_int(area, "widthPixels", width);
    if (!status) status = set_int(area, "heightPixels", height);
    if (!status) status = set_int(area, "originXPixels", x);
    if (!status) status = set_int(area, "originYPixels", y);
    if (status) { release_cf(area); return NULL; }
    return area;
}

static void *create_alt_display(void)
{
    CFArrayAppendValueFn append =
        (CFArrayAppendValueFn)sym("CFArrayAppendValue");
    void *display = create_dictionary();
    void *view = NULL, *safe = NULL, *view_areas = NULL;
    OSStatus status = display ? 0 : -1;

    if (!status) status = set_cstring(display, "uuid", ALT_UUID);
    if (!status) status = set_int(display, "type", 111);
    if (!status) status = set_int(display, "maxFPS", CNS2_DISPLAY_FPS);
    if (!status) status = set_int(display, "widthPixels", CNS2_DISPLAY_WIDTH);
    if (!status) status = set_int(display, "heightPixels", CNS2_DISPLAY_HEIGHT);
    if (!status) status = set_int(display, "widthPhysical", CNS2_PHYSICAL_WIDTH);
    if (!status) status = set_int(display, "heightPhysical", CNS2_PHYSICAL_HEIGHT);
    if (!status) status = set_int(display, "features", 10);
    if (!status) status = set_int(display, "primaryInputDevice", 3);
    if (!status) status = set_int(display, "initialViewArea", 0);
    if (!status) status = set_cstring(display, "initialURL", ALT_URL);
    if (!status) view = create_area(0, 0, CNS2_DISPLAY_WIDTH, CNS2_DISPLAY_HEIGHT);
    if (!status) safe = create_area(CNS2_SAFE_X, CNS2_SAFE_Y,
                                    CNS2_SAFE_WIDTH, CNS2_SAFE_HEIGHT);
    if (!status && (!view || !safe)) status = -1;
    if (!status) status = set_object(view, "safeArea", safe);
#if CNS2_SAFE_X != CNS2_NAV_X
    if (!status) status = set_adjacent(view,1);
#endif
    if (!status) view_areas = create_array();
    if (!status && (!view_areas || !append)) status = -1;
    if (!status) append(view_areas, view);
#if CNS2_SAFE_X != CNS2_NAV_X
    if(!status){
        void *second=create_area(0,0,CNS2_DISPLAY_WIDTH,CNS2_DISPLAY_HEIGHT);
        void *second_safe=create_area(CNS2_NAV_X,CNS2_SAFE_Y,
                                      CNS2_SAFE_WIDTH,CNS2_SAFE_HEIGHT);
        if(!second||!second_safe)status=-1;
        if(!status)status=set_object(second,"safeArea",second_safe);
        if(!status)status=set_adjacent(second,0);
        if(!status)append(view_areas,second);
        release_cf(second_safe);release_cf(second);
    }
#endif
    if (!status) status = set_object(display, "viewAreas", view_areas);

    release_cf(safe);
    release_cf(view);
    release_cf(view_areas);
    if (status) { release_cf(display); return NULL; }
    return display;
}

static OSStatus append_alt_display(void *server_info)
{
    CFArrayCreateMutableCopyFn copy =
        (CFArrayCreateMutableCopyFn)sym("CFArrayCreateMutableCopy");
    CFArrayGetCountFn count_fn =
        (CFArrayGetCountFn)sym("CFArrayGetCount");
    CFArrayAppendValueFn append =
        (CFArrayAppendValueFn)sym("CFArrayAppendValue");
    const void *old_displays = get_object(server_info, "displays");
    void *new_displays = NULL, *alt_display = NULL;
    OSStatus status = -1;

    if (!old_displays || !copy || !count_fn || !append) return -1;
    if (count_fn(old_displays) >= 2) return 1;
    new_displays = copy(NULL, 0, old_displays);
    alt_display = create_alt_display();
    if (new_displays && alt_display) {
        append(new_displays, alt_display);
        status = set_object(server_info, "displays", new_displays);
    }
    release_cf(alt_display);
    release_cf(new_displays);
    return status;
}

static OSStatus set_enabled_features(void *response)
{
    CFArrayAppendValueFn append =
        (CFArrayAppendValueFn)sym("CFArrayAppendValue");
    void *features = create_array();
    const char *names[] = { "iAPChannel", "viewAreas", "altScreen" };
    unsigned int i;
    OSStatus status = features && append ? 0 : -1;
    for (i = 0; !status && i < 3; ++i) {
        void *value = make_string(names[i]);
        if (!value) { status = -1; break; }
        append(features, value);
        release_cf(value);
    }
    if (!status) status = set_object(response, "enabledFeatures", features);
    release_cf(features);
    return status;
}

static OSStatus hook_session_setup(void *session, const void *request,
                                   void **response)
{
    const void *streams = request ? get_object(request, "streams") : NULL;
#ifdef CNS2_ALTSCREEN_PASSIVE_V2
    int stream_type = streams ? first_stream_type(request) : -1;
#endif
    OSStatus status;
    log_object(streams ? "stream-setup-request" : "session-setup-request",
               request, 0);
    status = SESSION_SETUP_REAL(session, request, response);
#ifdef CNS2_ALTSCREEN_PASSIVE_V2
    if (!status && streams && stream_type == 111 && response && *response) {
        uint16_t port = 0;
#ifdef CNS2_ALTSCREEN_AES_V3
        uint8_t aes_key[16], aes_iv[16];
        OSStatus id_status = -1;
        uint64_t connection_id = first_stream_connection_id(request,
                                                             &id_status);
        DeriveScreenAESFn derive = (DeriveScreenAESFn)sym(
            "AirPlay_DeriveAESKeySHA512ForScreen");
        int aes_valid = 0;
#ifdef CNS2_ALTSCREEN_CHACHA_V5
        uint8_t chacha_key[32];
        OSStatus chacha_status = -1;
        int chacha_valid = 0;
        memset(chacha_key, 0, sizeof(chacha_key));
#endif
        memset(aes_key, 0, sizeof(aes_key));
        memset(aes_iv, 0, sizeof(aes_iv));
#ifdef CNS2_ALTSCREEN_CHACHA_V5
        if (id_status == 0 && connection_id != 0) {
            chacha_status = derive_screen_chacha_key(session, connection_id,
                                                     chacha_key);
            chacha_valid = chacha_status == 0;
            log_values(chacha_valid ? "v5-chacha-derived" :
                       "v5-chacha-derive-unavailable",
                       chacha_status, chacha_valid, connection_id);
        }
#endif
        if (id_status == 0 && connection_id != 0 && derive) {
            const uint8_t *session_key = (const uint8_t *)session + 0x1F0U;
            derive(session_key, 16, connection_id, aes_key, aes_iv);
            aes_valid = 1;
            log_values("v3-aes-derived", 16, 16, connection_id);
        } else {
            log_values("v3-aes-derive-unavailable", id_status,
                       derive != NULL, connection_id);
        }
        OSStatus listener_status = start_passive_listener(&port, aes_key,
                                                           aes_iv, aes_valid
#ifdef CNS2_ALTSCREEN_CHACHA_V5
                                                           , chacha_key,
                                                           chacha_valid
#endif
#ifdef CNS2_ALTSCREEN_REFRESH
                                                           , session
#endif
                                                           );
#else
        OSStatus listener_status = start_passive_listener(&port);
#endif
        OSStatus response_status = listener_status >= 0 ?
            set_alt_stream_response(*response, port) : listener_status;
        log_values("v2-listener-result", listener_status, response_status,
                   port);
        log_object("v2-type111-response", *response, response_status);
    }
#endif
    if (!status && !streams && response && *response) {
        OSStatus patch_status = set_enabled_features(*response);
        log_object("enabled-features-result", *response, patch_status);
    }
#ifdef CNS2_ALTSCREEN_SHOWUI_V9
    if (!status && streams && stream_type == 110) {
        OSStatus showui_status = schedule_showui(session);
        log_object("v9-showui-scheduled", session, showui_status);
    }
#endif
    log_object(streams ? "stream-setup-response" : "session-setup-response",
               response ? *response : NULL, status);
    return status;
}

static void *hook_copy_server_info(void *server, const void *device_id,
                                   const void *interface_address,
                                   OSStatus *error)
{
    void *result = COPY_SERVER_INFO_REAL(server, device_id,
                                         interface_address, error);
    OSStatus status = result ? append_alt_display(result) : -1;
    log_object("server-info-declare-result", result, status);
    return result;
}

int arm_declare_v1(void)
{
    uintptr_t setup_now = *SESSION_SETUP_GOT;
    uintptr_t info_now = *COPY_SERVER_INFO_GOT;
    if (armed) return 1;
    if (setup_now != AIRPLAY_LAZY_RESOLVER &&
        setup_now != (uintptr_t)SESSION_SETUP_REAL) return -1;
    if (info_now != AIRPLAY_LAZY_RESOLVER &&
        info_now != (uintptr_t)COPY_SERVER_INFO_REAL) return -2;
    saved_session_setup = setup_now;
    saved_copy_server_info = info_now;
    *SESSION_SETUP_GOT = (uintptr_t)hook_session_setup;
    *COPY_SERVER_INFO_GOT = (uintptr_t)hook_copy_server_info;
    __sync_synchronize();
    armed = 1;
    log_object("declare-v1-armed", NULL, 0);
#ifdef CNS2_SKODA_ROLLER_CONTROL
    roller_start();
#endif
    return 0;
}

int disarm_declare_v1(void)
{
    if (!armed) return 1;
#ifdef CNS2_SKODA_ROLLER_CONTROL
    roller_disable();
#endif
    *SESSION_SETUP_GOT = saved_session_setup;
    *COPY_SERVER_INFO_GOT = saved_copy_server_info;
    __sync_synchronize();
    armed = 0;
    log_object("declare-v1-disarmed", NULL, 0);
    return 0;
}
