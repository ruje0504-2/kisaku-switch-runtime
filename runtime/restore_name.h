#ifndef KISAKU_RESTORE_NAME_H
#define KISAKU_RESTORE_NAME_H
#include <stddef.h>
#include <string.h>
/* Kisaku 4e2b50: uppercase the name and translate .ADV to .MES. */
static int krestore_name(char out[261],const char *name){
    if(!name||!name[0]||strlen(name)>260)return -1;
    size_t n=strlen(name);
    for(size_t i=0;i<=n;i++)out[i]=name[i]>='a'&&name[i]<='z'?name[i]-'a'+'A':name[i];
    char *ext=strrchr(out,'.');if(ext&&!strcmp(ext,".ADV"))memcpy(ext,".MES",5);
    return 0;
}
#endif
