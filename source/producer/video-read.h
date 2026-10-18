#ifndef CNS2_VIDEO_READ_H
#define CNS2_VIDEO_READ_H
/* Keep complete-packet silence separate from a truncated packet. Control
 * recovery runs even when no new frame arrives; never restart at a partial
 * header/body boundary. Callbacks make the actual read loop host-testable. */
typedef struct {
    void *opaque;
    uint64_t (*now)(void *);
    int (*pump)(void *);
    int (*wait)(int, void *);
    ssize_t (*receive)(int, void *, size_t, void *);
    uint64_t partial_timeout_us;
} Cns2VideoRead;
static int cns2_video_read_exact(Cns2VideoRead *io,int fd,uint8_t *data,
                                size_t size,int body_pending)
{
    size_t offset=0;
    uint64_t progress=io->now(io->opaque);
    while(offset<size){
        ssize_t n;
        int ready;
        if(io->pump(io->opaque)<0)return -3; /* real lifecycle end */
        ready=io->wait(fd,io->opaque);
        if(ready<0){if(errno==EINTR)continue;return -1;}
        if(!ready){
            uint64_t now=io->now(io->opaque);
            if((offset||body_pending)&&now>=progress&&
               now-progress>=io->partial_timeout_us)return -2;
            continue; /* no partial packet: static video is not disconnect */
        }
        n=io->receive(fd,data+offset,size-offset,io->opaque);
        if(n<0){if(errno==EINTR||errno==EAGAIN)continue;return -1;}
        if(!n)return -1; /* actual EOF */
        offset+=(size_t)n;
        progress=io->now(io->opaque);
    }
    return 0;
}
#endif
