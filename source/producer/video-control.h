#ifndef CNS2_VIDEO_CONTROL_H
#define CNS2_VIDEO_CONTROL_H
typedef struct {
    void *session;
    pid_t phone_pid;
    uint64_t next_tick,missing_since,next_idr;
    unsigned idr_attempts;
} Cns2VideoControl;
static uint64_t cns2_video_now(void *opaque){(void)opaque;return monotonic_us();}
static int cns2_video_pump(void *opaque)
{
    Cns2VideoControl *control=(Cns2VideoControl*)opaque;
    uint64_t now=monotonic_us();
    if(!armed)return -1;
    if(now<control->next_tick)return 0;
    control->next_tick=now+100000ULL;
    if(control->phone_pid&&!cns2_usb_phone_present(control->phone_pid)){
        if(!control->missing_since)control->missing_since=now;
        if(now-control->missing_since>=500000ULL){
            log_values("v4341-usb-phone-process-ended",(int)control->phone_pid,0,0);
            return -1;
        }
    }else control->missing_since=0;
    /* Unlike V43.4, recovery never waits for another video frame first. */
    if(consume_force_keyframe_marker()){
        control->idr_attempts=3;control->next_idr=now;
    }
    if(control->idr_attempts&&now>=control->next_idr){
        OSStatus result=send_force_keyframe(control->session);
        log_values("v4341-control-force-keyframe",result,(int)control->idr_attempts,0);
        --control->idr_attempts;control->next_idr=now+1000000ULL;
    }
    return 0;
}
static int cns2_video_wait(int fd,void *opaque)
{
    fd_set set;struct timeval timeout;(void)opaque;
    FD_ZERO(&set);FD_SET(fd,&set);timeout.tv_sec=0;timeout.tv_usec=100000;
    return select(fd+1,&set,NULL,NULL,&timeout);
}
static ssize_t cns2_video_receive(int fd,void *data,size_t size,void *opaque)
{(void)opaque;return recv(fd,data,size,0);}
#endif
