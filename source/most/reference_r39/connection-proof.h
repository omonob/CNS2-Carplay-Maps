#ifndef CNS2_CONNECTION_PROOF_H
#define CNS2_CONNECTION_PROOF_H
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#ifndef CNS2_DYNAMIC_TEST
#include <fcntl.h>
#include <devctl.h>
#include <sys/neutrino.h>
#include <sys/procfs.h>
#endif
struct cns2_connection { int fd; struct _server_info server; };
static int cns2_connection_read(int fd,struct cns2_connection *out)
{
    struct _server_info s;
    memset(&s,0,sizeof(s));
    if(fd<0 || ConnectServerInfo(0,fd,&s)!=fd || s.nd!=0 || s.pid<=0 ||
       (s.flags&_NTO_COF_DEAD)) return 0;
    out->fd=fd;out->server=s;return 1;
}
static int cns2_connection_same(const struct cns2_connection *a,const struct cns2_connection *b)
{
    return a->fd==b->fd && a->server.nd==b->server.nd && a->server.pid==b->server.pid &&
           a->server.chid==b->server.chid && a->server.scoid==b->server.scoid && a->server.coid==b->server.coid;
}
static int cns2_connection_live(const struct cns2_connection *a)
{
    struct cns2_connection now;
    return cns2_connection_read(a->fd,&now) && cns2_connection_same(a,&now);
}
#ifndef CNS2_DYNAMIC_TEST
static int cns2_transport_owner(const struct cns2_connection *connection)
{
    struct { procfs_debuginfo info; char extra[1024]; } reply;
    char name[64];int fd,rc;
    memset(&reply,0,sizeof(reply));
    snprintf(name,sizeof(name),"/proc/%ld/as",(long)connection->server.pid);
    fd=open(name,O_RDONLY);if(fd<0)return 0;
    rc=devctl(fd,DCMD_PROC_MAPDEBUG_BASE,&reply,sizeof(reply),NULL);owned_close(fd);
    if(rc || !memchr(reply.info.path,0,1024))return 0;
    if(strcmp(reply.info.path,"/extbin/apps/bin/TransportServer") &&
       strcmp(reply.info.path,"extbin/apps/bin/TransportServer"))return 0;
    return cns2_connection_live(connection);
}
#endif
#endif
