#include "flags.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
    assert(argc==2);
    uint8_t bytes[9192],srcbytes[9192],raw[15000];
    uint16_t words[600],srcwords[600];KValue values[100]={0};
    memset(bytes,9,sizeof(bytes));memset(srcbytes,0,sizeof(srcbytes));memset(raw,0xa5,sizeof(raw));
    for(unsigned i=0;i<600;i++){words[i]=10;srcwords[i]=500;}
    KFlags saved={0},current={0};
    saved.bytes=bytes;saved.byte_count=sizeof(bytes);saved.words=words;saved.word_count=600;
    saved.raw=raw;saved.raw_count=sizeof(raw);saved.globals[1]=values;saved.counts[1]=100;
    current=saved;current.bytes=srcbytes;current.words=srcwords;
    srcbytes[0]=255;srcbytes[3290]=2;srcbytes[3599]=3;srcbytes[9191]=128;
    assert(!kflags_merge(&saved,&current));
    assert(bytes[0]==255&&bytes[1]==9&&bytes[3289]==9&&bytes[3290]==2&&bytes[3291]==9);
    assert(bytes[3570]==9&&bytes[3594]==9&&bytes[3599]==3&&bytes[3600]==9&&bytes[9191]==128);
    assert(words[31]==10&&words[32]==500&&words[99]==500&&words[100]==10);
    srcbytes[3570]=1;assert(!kflags_merge(&saved,&current));
    assert(bytes[3570]==1&&bytes[3571]==0&&bytes[3594]==0&&bytes[3595]==9);
    values[61].number=1;srcbytes[3650]=2;srcbytes[3711]=255;
    assert(!kflags_merge(&saved,&current));
    assert(bytes[3649]==9&&bytes[3650]==2&&bytes[3651]==0&&bytes[3711]==255&&bytes[3712]==9);
    /* 5076f0 carries only catalog ranges; local story/raw/words stay in slot. */
    for(unsigned mode=0;mode<2;mode++){
        memset(bytes,11,sizeof(bytes));memset(srcbytes,23,sizeof(srcbytes));
        assert(!kflags_restore_progress(&saved,&current,mode));
        assert(bytes[1015]==11&&bytes[1016]==(mode?11:23)&&bytes[1017]==11);
        assert(bytes[1021]==11&&bytes[1022]==(mode?11:23)&&bytes[1028]==(mode?11:23)&&bytes[1029]==11);
        assert(bytes[1031]==11&&bytes[1032]==(mode?11:23)&&bytes[1159]==(mode?11:23)&&bytes[1160]==11);
        assert(bytes[1999]==11&&bytes[2000]==23&&bytes[4091]==23&&bytes[4092]==(mode?23:11)&&bytes[4095]==23&&bytes[4096]==11);
        assert(bytes[4007]==0&&bytes[4010]==0&&bytes[4011]==0&&bytes[4008]==23);
        assert(bytes[4999]==11&&bytes[5000]==23&&bytes[6999]==23&&bytes[7000]==11&&raw[1000]==0xa5&&words[31]==10);
    }
    current.byte_count=6999;memcpy(srcbytes,bytes,sizeof(bytes));
    assert(kflags_restore_progress(&saved,&current,0)<0&&!memcmp(bytes,srcbytes,sizeof(bytes)));
    current.byte_count=sizeof(srcbytes);
    uint8_t before[9192];memcpy(before,bytes,sizeof(bytes));current.word_count=99;
    assert(kflags_merge(&saved,&current)<0&&!memcmp(before,bytes,sizeof(bytes)));current.word_count=600;
    values[2]=(KValue){73,"CP932-independent typed value"};strcpy((char *)saved.module,"open.mes");
    assert(!kflags_write(&saved,argv[1]));KFlags *read=kflags_read(argv[1]);assert(read);
    assert(read->byte_count==9192&&read->word_count==600&&read->raw_count==15000);
    assert(!memcmp(read->bytes,bytes,sizeof(bytes))&&!memcmp(read->raw,raw,sizeof(raw)));
    assert(!memcmp(read->words,words,sizeof(words))&&!strcmp(read->globals[1][2].string,values[2].string));
    kflags_free(read);FILE *f=fopen(argv[1],"ab");assert(f);fputc(0,f);fclose(f);
    assert(!kflags_read(argv[1]));assert(!remove(argv[1]));
    puts("Kisaku FLAG ranges, merge modes, serialization and malformed input: PASS");return 0;
}
