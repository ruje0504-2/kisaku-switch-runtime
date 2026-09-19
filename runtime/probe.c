#include "ai6arc.h"
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
static int probe(const char *root) {
    const char *names[]={"data.arc","mes.arc","layer.arc","music.arc","effect.arc","voice.arc","movie.arc"};
    int failures=0;
    puts("KISAKU / AI6WIN resource diagnostic 0.1.0\nThis is NOT a playable game runtime.");
    for (unsigned i=0;i<7;i++) {
        char path[4096]; Ai6Archive a;
        int n=snprintf(path,sizeof(path),"%s/%s",root,names[i]);
        if (n<0 || (size_t)n>=sizeof(path) || ai6_open(&a,path)) {
            printf("FAIL %s: cannot open/invalid directory\n",names[i]); failures++; continue;
        }
        printf("OK   %-12s %u entries\n",names[i],a.count);
        if (i==1) {
            uint8_t *data=NULL;size_t size=0;
            if(ai6_read_named(&a,"startup.mes",&data,&size)){
                puts("FAIL startup.mes missing or invalid");failures++;
            }else{printf("OK   startup.mes decoded: %zu bytes\n",size);free(data);}
        }
        ai6_close(&a);
    }
    printf("Result: %d failure(s)\n",failures);
    return failures ? 1 : 0;
}
int main(int argc, char **argv) {
#ifdef __SWITCH__
    consoleInit(NULL);
    int result=probe("sdmc:/switch/kisaku/game");
    puts("\nUse the HOME menu to close.");
    while (appletMainLoop()) {
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return result;
#else
    if (argc==5 && !strcmp(argv[1],"--extract")) {
        Ai6Archive a; if (ai6_open(&a,argv[2])) return 1;
        int result=1;
        for (uint32_t j=0;j<a.count;j++) if (!strcmp(a.entries[j].name,argv[3])) {
            uint8_t *data; size_t size;
            if (!ai6_read(&a,j,&data,&size)) {
                FILE *f=fopen(argv[4],"wb");
                if (f) { int ok=fwrite(data,1,size,f)==size; if (fclose(f)) ok=0; result=ok?0:1; }
                free(data);
            }
            break;
        }
        ai6_close(&a); return result;
    }
    if (argc!=2) { fprintf(stderr,"Usage: %s ELFIMAGE\n       %s --extract archive name output\n",argv[0],argv[0]); return 2; }
    return probe(argv[1]);
#endif
}
