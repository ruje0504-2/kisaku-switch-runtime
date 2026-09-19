/* GPL-2.0-or-later. Host probe: the backlog / history page stores raw script
 * bytes, so it must decode them with the runtime's effective encoding (the
 * sticky, auto-detected GBK state), not with the configured encoding alone.
 * Before this wiring a translated pack rendered backlog lines as private-use
 * and half-width-katakana mojibake.
 * Usage: backlog-encoding-probe <ELFIMAGE> */
#include "bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cjk_or_ascii(uint32_t c){
    return c<128||(c>=0x3000&&c<=0x9fff)||(c>=0xff01&&c<=0xff60);
}

int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: %s <ELFIMAGE>\n",argv[0]);return 2;}
    /* A GBK-encoded backlog line, as a translation pack would leave it in the
       script stream: 原崎家的一族 完全版 */
    static const char gbk[]={0xBA,0xD3,0xD4,0xAD,0xC6,0xE9,0xBC,0xD2,0xB5,0xC4,
                             0xD2,0xBB,0xD7,0xE5,0x20,0xCD,0xEA,0xC8,0xAB,0xB0,0xE6,0};
    KTextChar wrong[64],right[64];size_t wrong_n=0,right_n=0;

    /* Control: decoding with the configured CP932 is exactly the old behaviour
       and must fail or land outside the CJK ranges. */
    int bad_decode=ktext_decode(KTEXT_CP932,(const uint8_t *)gbk,strlen(gbk),wrong,64,&wrong_n);
    size_t wrong_outside=0;
    for(size_t i=0;i<wrong_n;i++)if(!cjk_or_ascii(wrong[i].codepoint))wrong_outside++;

    KBootstrap *b=bootstrap_create(argv[1]);
    if(!b||b->error[0]){fprintf(stderr,"bootstrap failed: %s\n",b?b->error:"(null)");return 1;}

    /* A translated script set switches the session to GBK during the first
       message; the panels must follow that state. */
    b->text_gbk=1;
    if(bootstrap_text_encoding(b)!=KTEXT_GBK){fprintf(stderr,"effective encoding is not GBK\n");return 1;}
    if(bootstrap_decode_ui_text(b,gbk,strlen(gbk),right,64,&right_n)){fprintf(stderr,"UI decode failed\n");return 1;}

    size_t outside=0;
    for(size_t i=0;i<right_n;i++)if(!cjk_or_ascii(right[i].codepoint))outside++;
    int font_ok=bootstrap_ui_font(b)!=NULL;

    printf("cp932 控制组: rc=%d 字符=%zu 越界=%zu\n",bad_decode,(size_t)wrong_n,wrong_outside);
    printf("UI 有效编码解码: 字符=%zu 越界=%zu 字体=%s\n",right_n,outside,font_ok?"ok":"missing");
    printf("判定: %s\n",(right_n==11&&outside==0&&font_ok)?"PASS":"FAIL");
    return (right_n==11&&outside==0&&font_ok)?0:1;
}
