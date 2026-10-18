#ifndef ALTSCREEN_FRAME_PROTOCOL_H
#define ALTSCREEN_FRAME_PROTOCOL_H

#include <stdint.h>

#define ALT_FRAME_TYPE_CONFIG 1U
#define ALT_FRAME_TYPE_VIDEO 2U
#define ALT_FRAME_FLAG_KEYFRAME 0x00000001U

#define ALT_FRAME_RING_PATH "/ramdisk/carplay-alt-ring.bin"

/* Consumer-to-producer recovery signal. The NavInterface bridge creates this
 * marker only after it has discarded stale buffered video at a complete frame
 * boundary. The CarPlay producer consumes it and requests a genuine IDR from
 * the phone; predictive frames are never relabelled locally. */
#ifndef ALT_FRAME_FORCE_IDR_PATH
#define ALT_FRAME_FORCE_IDR_PATH "/ramdisk/carplay-alt-force-idr.request"
#endif

#define ALT_FRAME_RING_MAGIC 0x43505247U
#define ALT_FRAME_RING_VERSION 2U
#define ALT_FRAME_RING_SLOT_COUNT 64U
#define ALT_FRAME_RING_SLOT_DATA_MAX (256U * 1024U)

typedef struct AltFrameRingSlot {
    volatile uint32_t committed_sequence;
    uint32_t type;
    uint32_t size;
    uint32_t flags;
    uint32_t source_sequence;
    uint32_t media_timestamp_lo;
    uint32_t media_timestamp_hi;
    uint8_t data[ALT_FRAME_RING_SLOT_DATA_MAX];
} AltFrameRingSlot;

typedef struct AltFrameRing {
    volatile uint32_t magic;
    uint32_t version;
    volatile uint32_t generation;
    volatile uint32_t write_sequence;
    volatile uint32_t session_active;
    volatile uint32_t reserved;
    AltFrameRingSlot slots[ALT_FRAME_RING_SLOT_COUNT];
} AltFrameRing;

#endif
