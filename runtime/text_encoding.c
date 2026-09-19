#include "text_encoding.h"
#include "codepages.inc"
int ktext_decode(KTextEncoding encoding,const uint8_t *input,size_t size,KTextChar *out,size_t capacity,size_t *count){
    if(!count)return -1;
    *count=0;if((encoding!=KTEXT_CP932&&encoding!=KTEXT_GBK)||(!input&&size)||(!out&&capacity))return -1;
    const uint16_t *single=encoding==KTEXT_GBK?gbk_single:cp932_single;
    const uint16_t *pairs=encoding==KTEXT_GBK?gbk_double:cp932_double;
    size_t n=0;
    for(size_t i=0;i<size;){
        unsigned a=input[i++],cp=single[a],columns=1;
        if(!cp&&a){
            if(a<0x81||a>0xfe||i==size)return -1;
            unsigned b=input[i++];if(b<0x40||b>0xfe||b==0x7f)return -1;
            cp=pairs[(a-0x81)*190+b-0x40-(b>0x7f)];columns=2;
            if(!cp)return -1;
        }
        if(n==capacity)return -1;
        out[n++]=(KTextChar){cp,columns};
    }
    *count=n;return 0;
}
int ktext_private_use(const KTextChar *chars,size_t count){
    for(size_t i=0;i<count;i++)if(chars[i].codepoint>=0xe000&&chars[i].codepoint<=0xf8ff)return 1;
    return 0;
}
/* Suspicion score for "this table was the wrong one". A failed decode scores
 * worst; private-use output (undefined CP932 leads) is a strong signal, and a
 * run dominated by half-width katakana is a weaker one: GBK lead bytes
 * 0xA1-0xDF decode as single-byte katakana in CP932, while genuine Japanese text
 * uses them sparingly, so they only count when they dominate the run. */
static int suspicion(const KTextChar *chars,size_t count){
    int score=0,kana=0;
    for(size_t i=0;i<count;i++){
        uint32_t cp=chars[i].codepoint;
        if(cp>=0xe000&&cp<=0xf8ff)score+=4;
        else if(cp>=0xff61&&cp<=0xff9f)kana++;
    }
    if(count&&(size_t)kana*3>=count)score+=kana;
    return score;
}
int ktext_decode_auto(KTextEncoding configured,int auto_fallback,const uint8_t *input,size_t size,
                      KTextChar *out,size_t capacity,size_t *count,KTextEncoding *used){
    const int FAILED=1<<20;
    *count=0;if(used)*used=configured;
    int primary=ktext_decode(configured,input,size,out,capacity,count);
    size_t primary_count=*count;
    int primary_score=primary?FAILED:suspicion(out,primary_count);
    if(!auto_fallback)return primary;
    if(primary_score==0)return 0;
    /* A translated script decoded with the wrong table fails outright or leaves
     * katakana/private-use debris; compare against the other table. */
    KTextEncoding alternate=configured==KTEXT_CP932?KTEXT_GBK:KTEXT_CP932;
    int alt=ktext_decode(alternate,input,size,out,capacity,count);
    int alt_score=alt?FAILED:suspicion(out,*count);
    if(primary_score<=alt_score){
        if(primary){*count=0;return -1;}
        if(ktext_decode(configured,input,size,out,capacity,count)){*count=0;return -1;}
        return 0;
    }
    if(alt){*count=0;return -1;}
    if(used)*used=alternate;
    return 0;
}
/* Strict UTF-8 input for the name editor. ASCII is converted to full-width
   CP932 characters because the native inline-name reader treats low bytes as
   script opcodes. Output length is measured in original-game bytes. */
int ktext_name_encode(const char *input,uint8_t out[33],size_t *size){
    if(!input||!out||!size)return -1;
    const uint8_t *p=(const uint8_t *)input;size_t n=0;
    while(*p){
        uint32_t cp=*p++;unsigned more=0;uint32_t minimum=0;
        if(cp>=0xc2&&cp<=0xdf){cp&=31;more=1;minimum=0x80;}
        else if(cp>=0xe0&&cp<=0xef){cp&=15;more=2;minimum=0x800;}
        else if(cp>=0xf0&&cp<=0xf4){cp&=7;more=3;minimum=0x10000;}
        else if(cp>=0x80)return -1;
        for(unsigned i=0;i<more;i++){if((*p&0xc0)!=0x80)return -1;cp=(cp<<6)|(*p++&63);}
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||cp<0x20||cp==0x7f)return -1;
        if(cp==0x20)cp=0x3000;else if(cp<0x7f)cp+=0xfee0;
        unsigned value=0;
        for(unsigned i=0xa1;i<=0xdf;i++)if(cp932_single[i]==cp){value=i;break;}
        if(!value)for(unsigned i=0;i<126*190;i++)if(cp932_double[i]==cp){unsigned trail=i%190+0x40;if(trail>=0x7f)trail++;value=((i/190+0x81)<<8)|trail;break;}
        if(!value||n+(value>255?2:1)>32)return -1;
        if(value>255)out[n++]=(uint8_t)(value>>8);
        out[n++]=(uint8_t)value;
    }
    if(!n)return -1;
    out[n]=0;*size=n;return 0;
}
