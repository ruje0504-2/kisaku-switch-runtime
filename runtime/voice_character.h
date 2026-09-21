#ifndef KISAKU_VOICE_CHARACTER_H
#define KISAKU_VOICE_CHARACTER_H
#include <ctype.h>
#include <stdlib.h>
/* 464140: Kisaku's 33 character channels; 466630 exempts no-* resources. */
static inline int kisaku_voice_character(const char *name){
    if(!name||!name[0])return 0;
    if(name[0]=='n'&&name[1]=='o'&&name[2]=='-')return -1;
    static const char prefixes[]="zhibjrcemtdfgkl";
    static const unsigned channels[]={0,1,2,3,4,5,6,7,8,9,11,13,14,15,16};
    int first=tolower((unsigned char)name[0]);
    if(first=='a')return 10;
    if(first=='s')return 12;
    for(unsigned i=0;i<sizeof(channels)/sizeof(*channels);i++)if(first==prefixes[i])return (int)channels[i];
    if(first=='n'){
        long n=strtol(name+1,NULL,10);
        static const unsigned last[]={36,53,54,61,64,102,205,210,222,232,263,274,323,332,337,343};
        static const unsigned ids[]={28,29,30,31,32,17,18,19,20,21,22,23,24,25,26,27};
        if(n>0)for(unsigned i=0;i<sizeof(last)/sizeof(*last);i++)if(n<=(long)last[i])return (int)ids[i];
    }
    return 0;
}
#endif
