#ifndef KISAKU_FILE_STORE_H
#define KISAKU_FILE_STORE_H
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#if defined(__SWITCH__)
#include "switch_hos.h"
#endif
/* Horizon does not replace a destination in rename(). Keep the last complete
   file as a backup until the new file is committed; recover after interruption.
   Call only for files owned by this runtime, never original game saves. */
static inline int kstore_recover(const char *path){
#if defined(__SWITCH__) || defined(KSTORE_NO_REPLACE)
    char backup[4120];struct stat st;
    if(snprintf(backup,sizeof(backup),"%s.bak",path)>=(int)sizeof(backup))return -1;
    if(!stat(path,&st))return 0;
    if(errno!=ENOENT)return -1;
    if(!stat(backup,&st))return rename(backup,path);
    if(errno!=ENOENT)return -1;
#else
    (void)path;
#endif
    return 0;
}
static inline int kstore_replace(const char *temporary,const char *path){
#if defined(__SWITCH__) || defined(KSTORE_NO_REPLACE)
    char backup[4120];struct stat st;int exists;
    if(kstore_recover(path)||snprintf(backup,sizeof(backup),"%s.bak",path)>=(int)sizeof(backup))return -1;
    exists=!stat(path,&st);if(!exists&&errno!=ENOENT)return -1;
    if(exists&&!S_ISREG(st.st_mode)){errno=EINVAL;return -1;}
    if(exists){
        if(remove(backup)&&errno!=ENOENT)return -1;
        if(rename(path,backup))return -1;
    }
    if(rename(temporary,path)){
        int saved=errno;if(exists)rename(backup,path);errno=saved;return -1;
    }
    if(exists)remove(backup);
#if defined(__SWITCH__)
    /* libnx requires an explicit fsdevCommitDevice after SaveData writes; it is
       done neither on close nor on unmount.  No-op for the SD-card layout. */
    switch_hos_commit();
#endif
    return 0;
#else
    return rename(temporary,path);
#endif
}
#endif
