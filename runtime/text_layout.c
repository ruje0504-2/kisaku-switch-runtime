#include "text_layout.h"
#include <stdint.h>
#include <limits.h>
static int contains(const uint32_t *table,size_t n,uint32_t cp){for(size_t i=0;i<n;i++)if(table[i]==cp)return 1;return 0;}
static int layout(const KTextChar *chars,size_t count,int left,int top,int right,int bottom,int half,int line,int *x,int *y,KTextPosition *positions,int bounded){
    static const uint32_t close[]={0xff09,0x3015,0xff3d,0xff5d,0x3009,0x300b,0x300d,0x300f,0x3011,0x3002,0x3001,0x30fb,0xff1f,0xff01};
    static const uint32_t open[]={0xff08,0x3014,0xff3b,0xff5b,0x3008,0x300a,0x300c,0x300e,0x3010};
    if(!x||!y||(!chars&&count)||(!positions&&count)||left<0||top<0||right<=left||right>4096||bottom<=top||bottom>4096||half<1||half>128||line<1||line>256||right-left<half*2||*x<left||*x>right+half*2||*y<top||(bounded&&*y>bottom))return -1;
    int64_t px=*x,py=*y;int adjusted=0;
    for(size_t i=0;i<count;i++){
        if(chars[i].columns<1||chars[i].columns>2||chars[i].codepoint<32)return -1;
        if(px>=right){px=left;py+=line;adjusted=0;}
        if(px==left&&py!=top){
            if(!adjusted&&contains(close,sizeof(close)/sizeof(*close),chars[i].codepoint)){
                px=right;if(py>top)py-=line;adjusted=1;
            }else adjusted=0;
        }
        if(px==right-half*2){
            if(!adjusted&&contains(open,sizeof(open)/sizeof(*open),chars[i].codepoint)){
                px=left;py+=line;adjusted=1;
            }else adjusted=0;
        }
        if(bounded&&py+line>bottom)return 1;
        if(py>INT_MAX)return -1;
        positions[i]=(KTextPosition){(int)px,(int)py};px+=chars[i].columns*half;
    }
    *x=(int)px;*y=(int)py;return 0;
}

int ktext_layout(const KTextChar *chars,size_t count,int left,int top,int right,int bottom,int half,int line,int *x,int *y,KTextPosition *positions){
    return layout(chars,count,left,top,right,bottom,half,line,x,y,positions,1);
}
int ktext_layout_native(const KTextChar *chars,size_t count,int left,int top,int right,int bottom,int half,int line,int *x,int *y,KTextPosition *positions){
    return layout(chars,count,left,top,right,bottom,half,line,x,y,positions,0);
}
