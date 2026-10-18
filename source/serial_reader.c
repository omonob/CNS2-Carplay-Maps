/* Read-only early manufacturer cache. No WiFi/ifconfig, fork or ioctl. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#include "monocypher.h"

int cns2_read_serial(char serial[24])
{
    void *z=NULL,*sql=NULL,*gz=NULL,*db=NULL,*stmt=NULL,*icu[3]={NULL,NULL,NULL};
    const char *paths[3]={"/ifs2/libicudata.so","/ifs2/libicuuc.so","/ifs2/libicui18n.so"};
    unsigned library;
    void *(*gzopen_fn)(const char*,const char*);
    int (*gzread_fn)(void*,void*,unsigned),(*gzclose_fn)(void*)=NULL;
    int (*open_fn)(const char*,void**,int,const char*),(*prep_fn)(void*,const char*,int,void**,const char**);
    int (*step_fn)(void*),(*bytes_fn)(void*,int),(*int_fn)(void*,int);
    const void *(*blob_fn)(void*,int);
    int (*final_fn)(void*)=NULL,(*close_fn)(void*)=NULL;
    unsigned char b[262145];size_t used=0,done=0;
    char temp[]="/ramdisk/.cns2-serial-id.XXXXXX";int fd=-1,rc=50,n,created=0;
    z=dlopen("/proc/boot/libz.so.2",RTLD_NOW|RTLD_LOCAL);
    if(!z) {fprintf(stderr,"CNS2_SERIAL_ZLIB_LOAD: %s\n",dlerror());goto cleanup;}
    /* Stock SQLite needs ICU symbols, only loaded in this isolated process. */
    for(library=0;library<3;library++) {
        icu[library]=dlopen(paths[library],RTLD_NOW|RTLD_GLOBAL);
        if(!icu[library]) {fprintf(stderr,"CNS2_SERIAL_ICU_LOAD: %s\n",paths[library]);goto cleanup;}
    }
    sql=dlopen("/extbin/apps/lib/libsqlite.so",RTLD_NOW|RTLD_LOCAL);
    if(!sql) {fprintf(stderr,"CNS2_SERIAL_SQLITE_LOAD: %s\n",dlerror());goto cleanup;}
#define LOAD(h,fn,sym) do {*(void **)(&(fn))=dlsym(h,sym);if(!(fn))goto cleanup;} while(0)
    LOAD(z,gzopen_fn,"gzopen");LOAD(z,gzread_fn,"gzread");LOAD(z,gzclose_fn,"gzclose");
    LOAD(sql,open_fn,"sqlite3_open_v2");LOAD(sql,prep_fn,"sqlite3_prepare_v2");
    LOAD(sql,step_fn,"sqlite3_step");LOAD(sql,bytes_fn,"sqlite3_column_bytes");
    LOAD(sql,int_fn,"sqlite3_column_int");LOAD(sql,blob_fn,"sqlite3_column_blob");
    LOAD(sql,final_fn,"sqlite3_finalize");LOAD(sql,close_fn,"sqlite3_close");
    gz=gzopen_fn("/persistence/vip_sys_db.sql.gz","rb");if(!gz) {rc=51;goto cleanup;}
    while(used<sizeof(b)) {
        n=gzread_fn(gz,b+used,(unsigned)(sizeof(b)-used));
        if(!n)break;
        if(n<0) {rc=52;goto cleanup;}
        used+=(size_t)n;
    }
    n=gzclose_fn(gz);gz=NULL;
    if(n || used<100 || used>=sizeof(b) || memcmp(b,"SQLite format 3\0",16)) {rc=53;goto cleanup;}
    fd=mkstemp(temp);if(fd<0) {rc=54;goto cleanup;}created=1;
    while(done<used) {
        ssize_t w=write(fd,b+done,used-done);
        if(w<0 && errno==EINTR)continue;
        if(w<=0) {rc=55;goto cleanup;}done+=(size_t)w;
    }
    if(close(fd)) {fd=-1;rc=55;goto cleanup;}fd=-1;
    if(open_fn(temp,&db,1,NULL) || prep_fn(db,
       "SELECT obj_size,CAST(obj_data AS BLOB) FROM vipDB WHERE obj_id=121",-1,&stmt,NULL)) {rc=56;goto cleanup;}
    if(step_fn(stmt)!=100 || int_fn(stmt,0)!=23 || bytes_fn(stmt,1)!=23) {rc=57;goto cleanup;}
    memcpy(serial,blob_fn(stmt,1),23);serial[23]=0;
    if(step_fn(stmt)!=101) {rc=58;goto cleanup;}rc=0;
cleanup:
    if(stmt)final_fn(stmt);
    if(db)close_fn(db);
    if(gz)gzclose_fn(gz);
    if(fd>=0)close(fd);
    if(created)unlink(temp);
    crypto_wipe(b,sizeof(b));
    if(sql)dlclose(sql);
    for(library=3;library>0;library--)if(icu[library-1])dlclose(icu[library-1]);
    if(z)dlclose(z);
    return rc;
}
