/* SPDX-License-Identifier: MIT
 * Installation-only helper. No licensing secret, JAR, activation-message,
 * audio metadata, navigation metadata or firmware image is embedded here.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "monocypher.h"
#include "rc-delta.h"
int cns2_read_serial(char serial[24]);
static int digest(const char *path,char hex[65],unsigned long *bytes)
{
    struct stat before,after;crypto_blake2b_ctx ctx;
    uint8_t block[65536],hash[32];size_t n;unsigned i;unsigned long total=0;
    if(lstat(path,&before)||!S_ISREG(before.st_mode)||before.st_size<0||before.st_size>64*1024*1024)return 10;
    FILE *f=fopen(path,"rb");if(!f)return 11;
    crypto_blake2b_init(&ctx,32);
    while((n=fread(block,1,sizeof(block),f))){crypto_blake2b_update(&ctx,block,n);total+=(unsigned long)n;}
    int bad=ferror(f);fclose(f);
    if(bad||lstat(path,&after)||before.st_ino!=after.st_ino||before.st_size!=after.st_size||before.st_mtime!=after.st_mtime||total!=(unsigned long)before.st_size)return 12;
    crypto_blake2b_final(&ctx,hash);
    for(i=0;i<32;i++)sprintf(hex+2*i,"%02x",hash[i]);
    hex[64]=0;*bytes=total;return 0;
}
static int check_list(const char *list,const char *root)
{
    char line[1024],name[256],hash[65],got[65],path[1024],extra,seen[128][256];
    unsigned mode,count=0,i;unsigned long bytes,n;int rc;
    struct stat st;if(lstat(list,&st)||!S_ISREG(st.st_mode))return 20;
    FILE *f=fopen(list,"r");if(!f)return 20;
    while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n')||count>=128||sscanf(line,"%255s %o %lu %64s %c",name,&mode,&bytes,hash,&extra)!=4||strlen(hash)!=64||strstr(name,"..")){fclose(f);return 21;}
        for(i=0;i<64;i++)if(!strchr("0123456789abcdef",hash[i])){fclose(f);return 21;}
        for(i=0;i<count;i++)if(!strcmp(name,seen[i])){fclose(f);return 22;}
        strcpy(seen[count++],name);
        if(root){if(name[0]=='/'||snprintf(path,sizeof(path),"%s/%s",root,name)>=(int)sizeof(path)){fclose(f);return 21;}}
        else {if(name[0]!='/'){fclose(f);return 21;}strcpy(path,name);}
        rc=digest(path,got,&n);
        if(rc||n!=bytes||strcmp(hash,got)){fprintf(stderr,"FILE_REJECT %s\n",name);fclose(f);return 23;}
        printf("FILE_OK %s bytes=%lu\n",name,n);
    }
    int bad=ferror(f);fclose(f);return bad||!count?24:0;
}
static int write_new(const char *path,const void *data,size_t n,mode_t mode)
{
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,mode);if(fd<0)return 30;
    const uint8_t *p=data;size_t done=0;int bad=0;
    while(done<n){ssize_t w=write(fd,p+done,n-done);if(w<0&&errno==EINTR)continue;if(w<=0){bad=1;break;}done+=(size_t)w;}
    if(!bad&&(fchmod(fd,mode)||fsync(fd)))bad=1;
    if(close(fd))bad=1;
    if(bad){unlink(path);return 31;}return 0;
}
static int patch_rc(const char *src,const char *dst)
{
    char hex[65];unsigned long n;uint8_t data[3732];struct stat st;
    if(digest(src,hex,&n)||n!=sizeof(data)||strcmp(hex,"b303244618ad8728e3609d0700a173eacc2885a71fb499e57b88e0c86741b648")||lstat(src,&st))return 40;
    FILE *f=fopen(src,"rb");if(!f)return 40;
    size_t got=fread(data,1,sizeof(data),f);int bad=ferror(f);fclose(f);if(got!=sizeof(data)||bad)return 40;
    for(size_t i=0;i<sizeof(rc_delta)/sizeof(rc_delta[0]);i++){
        if(data[rc_delta[i].offset]!=rc_delta[i].before)return 41;
        data[rc_delta[i].offset]=rc_delta[i].after;
    }
    uint8_t hash[32];crypto_blake2b(hash,32,data,sizeof(data));
    for(unsigned i=0;i<32;i++)sprintf(hex+i*2,"%02x",hash[i]);
    hex[64]=0;
    if(strcmp(hex,"8591e0a88dbbde53cc9425e1d4a62f8dab52bef1ae0d8e2c9cecd6e307c5d205"))return 42;
    return write_new(dst,data,sizeof(data),st.st_mode&0777);
}
/* Existing four short entry paths are intentionally retained for paid updates.
 * Native arguments, native files and every unrelated procman line stay intact.
 */
static int patch_pm(const char *src,const char *dst)
{
    FILE *in=fopen(src,"r"),*out;char line[4096];unsigned counts[4]={0};int bad=0;
    const char *ids[]={"-n\"hmi\"","-n\"most\"","-n\"carplay\"","-n\"navi_interface\""};
    const char *stock[]={"runHMI.sh","Most","CarPlay","NavInterface"};
    const char *target[]={"/persistence/.h","/persistence/.m","/persistence/.c","/persistence/.n"};
    if(!in)return 60;
    int fd=open(dst,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0){fclose(in);return 61;}
    out=fdopen(fd,"w");if(!out){close(fd);unlink(dst);fclose(in);return 61;}
    while(fgets(line,sizeof(line),in)){
        unsigned i;int found=0;if(!strchr(line,'\n')){bad=1;break;}
        if(line[0]!='#')for(i=0;i<4;i++)if(strstr(line,ids[i])){
            char *flag=strstr(line,"-f"),*first,*end;size_t len;
            if(found++||!flag){bad=1;break;}
            first=flag+2;while(*first==' '||*first=='\t')first++;
            end=first;while(*end&&*end!=' '&&*end!='\t'&&*end!='\r'&&*end!='\n')end++;
            len=(size_t)(end-first);
            if(!((len==strlen(stock[i])&&!memcmp(first,stock[i],len))||
                 (i==0&&len==strlen("/extbin/apps/hmi/HMI/runHMI.sh")&&!memcmp(first,"/extbin/apps/hmi/HMI/runHMI.sh",len))||
                 (len==strlen(target[i])&&!memcmp(first,target[i],len)))){bad=1;break;}
            if(++counts[i]!=1){bad=1;break;}
            if(fwrite(line,1,(size_t)(first-line),out)!=(size_t)(first-line)||fputs(target[i],out)==EOF||fputs(end,out)==EOF)bad=1;
        }
        if(bad)break;
        if(!found&&fputs(line,out)==EOF){bad=1;break;}
    }
    if(ferror(in))bad=1;
    fclose(in);
    if(fflush(out)||fsync(fileno(out)))bad=1;
    if(fclose(out))bad=1;
    for(unsigned i=0;i<4;i++)if(counts[i]!=1)bad=1;
    if(bad){unlink(dst);return 62;}puts("V43_PM_PATCH_OK four paths only");return 0;
}
static int ssh_probe(void)
{
    int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 70;
    struct sockaddr_in peer;memset(&peer,0,sizeof(peer));peer.sin_family=AF_INET;
    peer.sin_port=htons(2222);peer.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(struct sockaddr*)&peer,sizeof(peer))){close(fd);return 71;}
    fd_set set;FD_ZERO(&set);FD_SET(fd,&set);struct timeval wait={3,0};char banner[128];
    if(select(fd+1,&set,NULL,NULL,&wait)<=0){close(fd);return 72;}
    ssize_t n=recv(fd,banner,sizeof(banner),0);close(fd);
    if(n<8||memcmp(banner,"SSH-2.0-",8))return 73;
    puts("SSH_BANNER_ON_2222_OK");return 0;
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--identity")){
        char id[24];int rc;
#ifdef HOST_TEST
        const char *fixture=getenv("CNS2_FIXTURE_SERIAL");if(!fixture)return 50;
        if(strlen(fixture)!=23)return 51;
        memcpy(id,fixture,24);rc=0;
#else
        rc=cns2_read_serial(id);
#endif
        if(rc)return rc;
        if(strncmp(id,"7WJ-",4)||strlen(id)!=23||id[9]!='.'||id[12]!='.')return 52;
        for(unsigned i=4;i<23;i++)if(i!=9&&i!=12&&(id[i]<'0'||id[i]>'9'))return 52;
        printf("SERIAL=%s\n",id);return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--generation")){
        uint8_t b[8];FILE *f=fopen("/dev/urandom","rb");if(!f)return 80;
        size_t n=fread(b,1,sizeof(b),f);fclose(f);if(n!=sizeof(b))return 80;
        for(unsigned i=0;i<sizeof(b);i++)printf("%02x",b[i]);
        puts("");return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--ssh-probe"))return ssh_probe();
    if(argc==3&&!strcmp(argv[1],"--digest")){char hex[65];unsigned long n;int rc=digest(argv[2],hex,&n);if(!rc)printf("%s %lu\n",hex,n);return rc;}
    if(argc==3&&!strcmp(argv[1],"--check-native"))return check_list(argv[2],NULL);
    if(argc==4&&!strcmp(argv[1],"--check-list"))return check_list(argv[2],argv[3]);
    if(argc==4&&!strcmp(argv[1],"--patch-pm"))return patch_pm(argv[2],argv[3]);
    if(argc==4&&!strcmp(argv[1],"--prepare-rc"))return patch_rc(argv[2],argv[3]);
    return 2;
}
