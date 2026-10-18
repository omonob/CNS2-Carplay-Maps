/* Video-only edition: no animation assets, parser, playback or preview. */
#define R433_PREVIEW_PATH "/ramdisk/.cns2-free-preview-not-supported"
typedef struct {
    void *data;
    unsigned active,preview,frames,total_frames,completed,aborted,seen;
} R433Intro;
static R433Intro r433_intro;
static void r436_intro_session(unsigned active) {(void)active;}
static void r433_intro_load(void) {}
static void r433_intro_abort(const char *reason) {(void)reason;}
static int r433_source_eligible(void) {return r41_phone != 0;}
static int r433_intro_begin(int preview,unsigned generation,uint64_t now)
{(void)preview;(void)generation;(void)now;return 0;}
static int r433_intro_tick(uint64_t now) {(void)now;return 0;}
