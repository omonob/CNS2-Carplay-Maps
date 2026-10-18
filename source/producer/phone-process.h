#ifndef CNS2_PHONE_PROCESS_H
#define CNS2_PHONE_PROCESS_H
/* Read-only QNX executable identity. Never stop/signal a process. Only bind
 * an exact single mm-ipod observed when this TCP session was established.
 * No observed USB process means no USB-presence assumption (wireless case). */
#include <dirent.h>
#include <devctl.h>
#include <sys/procfs.h>
static int cns2_process_name(pid_t pid,char *out,size_t capacity)
{
    char path[64]; int fd,result;
    union { uint64_t alignment; char bytes[sizeof(procfs_debuginfo)+512]; } info;
    procfs_debuginfo *debug=(procfs_debuginfo*)info.bytes;
    if(pid<=0||capacity<2)return -1;
    memset(&info,0,sizeof(info));
    snprintf(path,sizeof(path),"/proc/%ld/as",(long)pid);
    fd=open(path,O_RDONLY);if(fd<0)return -1;
    result=devctl(fd,DCMD_PROC_MAPDEBUG_BASE,info.bytes,sizeof(info.bytes),NULL);
    close(fd);if(result)return -1;
    info.bytes[sizeof(info.bytes)-1]=0;
    snprintf(out,capacity,"%s",debug->path);return 0;
}
static pid_t cns2_find_usb_phone(void)
{
    DIR *dir=opendir("/proc");struct dirent *entry;pid_t found=0;
    if(!dir)return 0;
    while((entry=readdir(dir))!=NULL){
        char *end,name[512];long value;const char *base;
        if(entry->d_name[0]<'0'||entry->d_name[0]>'9')continue;
        value=strtol(entry->d_name,&end,10);
        if(*end||value<=0||value>0x7fffffffL||cns2_process_name((pid_t)value,name,sizeof(name)))continue;
        base=strrchr(name,'/');base=base?base+1:name;
        if(strcmp(base,"mm-ipod"))continue;
        if(found){found=0;break;} /* ambiguous, do not guess */
        found=(pid_t)value;
    }
    closedir(dir);return found;
}
static int cns2_usb_phone_present(pid_t pid)
{
    char path[64];struct stat st;
    snprintf(path,sizeof(path),"/proc/%ld/as",(long)pid);
    if(!stat(path,&st))return 1;
    return errno==ENOENT||errno==ESRCH?0:1; /* transient permission/I/O is not unplug */
}
#endif
