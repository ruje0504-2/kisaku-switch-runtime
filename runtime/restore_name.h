#ifndef KISAKU_RESTORE_NAME_H
#define KISAKU_RESTORE_NAME_H
#include <stddef.h>
#include <string.h>
/* 407a70: only the active script is renamed in alternate restore mode.
   _splitpath removes its path; a trailing lowercase _m is removed first.
   byte8190 selects the ordinary (0) or _m (1) extension-bearing name. */
static int krestore_name(char out[261],const char *name,int active,unsigned alternate,unsigned mode){
    if(!name||!name[0]||strlen(name)>260)return -1;
    if(!active||alternate!=1){strcpy(out,name);return 0;}
    const char *base=name;
    for(const char *p=name;*p;p++)if(*p=='/'||*p=='\\'||*p==':')base=p+1;
    const char *ext=strrchr(base,'.');if(!ext)ext=base+strlen(base);
    size_t n=(size_t)(ext-base);
    if(n>=2&&base[n-2]=='_'&&base[n-1]=='m')n-=2;
    size_t suffix=mode==1?2:0,tail=mode<=1?strlen(ext):0;
    if(!n||n+suffix+tail>260)return -1;
    memcpy(out,base,n);
    if(suffix){out[n++]='_';out[n++]='m';}
    memcpy(out+n,ext,tail);out[n+tail]=0;return 0;
}
#endif
