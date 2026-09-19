/* 40f340 / 40f4e0: ordered AREA rectangles, first hit wins. */
static void area_prepare(KBootstrap *b);
static int area_begin(KBootstrap *b,const char *name){
    uint8_t *data=NULL;size_t n=0;if(read_named(&b->data,name,&data,&n))return error(b,"AREA resource missing");
    if(n<4){free(data);return error(b,"AREA header truncated");}
    uint32_t count=(uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;
    if(count>512||n!=4+(size_t)count*20){free(data);return error(b,"AREA size invalid");}
    for(unsigned i=0;i<count;i++)for(unsigned j=0;j<5;j++){
        const uint8_t *p=data+4+i*20+j*4;b->areas[i][j]=(int32_t)((uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24);
    }
    free(data);snprintf(b->area_name,sizeof(b->area_name),"%s",name);b->area_count=count;b->area_selected=-1;b->area_active=1;area_prepare(b);return 0;
}
static int area_hit(const KBootstrap *b,int x,int y){
    for(unsigned i=0;i<b->area_count;i++){const int32_t *a=b->areas[i];if(x>=a[1]&&x<a[3]&&y>=a[2]&&y<a[4])return (int)i;}return -1;
}
static void area_finish(KBootstrap *b,int cancel){
    if(b->area_active==2&&cancel){unsigned count=b->vm->bytes[1951];b->vm->bytes[1950]=(uint8_t)(count?(b->vm->bytes[1950]+1)%count:0);return;}
    if(!cancel&&(b->area_selected<0||(unsigned)b->area_selected>=b->area_count))return;
    unsigned value=cancel?0:(unsigned)b->areas[b->area_selected][0];
    b->vm->globals[0][13]=(KValue){(int32_t)(value&255),NULL};
    b->vm->globals[0][21]=(KValue){b->area_active==2?b->vm->bytes[1950]:(int32_t)((value>>8)&255),NULL};
    b->vm->globals[0][18]=(KValue){cancel?0:65535,NULL};
    if(cancel)b->vm->bytes[1010]=1;
    b->area_active=0;
}
/* Earlier rectangles can cover the center while leaving a clickable strip.
   Examine each horizontal band bounded by an earlier rectangle's bottom edge,
   then skip covered x intervals. The first-match hit rule remains unchanged. */
static int area_point(const KBootstrap *b,unsigned index,int *px,int *py){
    const int32_t *a=b->areas[index];
    int left=a[1]<0?0:a[1],right=a[3]>640?640:a[3];
    int top=a[2]<0?0:a[2],bottom=a[4]>480?480:a[4];
    if(left>=right||top>=bottom)return 0;
    int x=(left+right)/2,y=(top+bottom)/2,hit=area_hit(b,x,y);
    if(hit>=0&&b->areas[hit][0]==a[0]){*px=x;*py=y;return 1;}
    for(unsigned band=0;band<=index;band++){
        y=band?b->areas[band-1][4]:top;
        if(y<top||y>=bottom)continue;
        x=left;
        while(x<right){
            unsigned i=0;
            for(;i<index;i++){
                const int32_t *cover=b->areas[i];
                if(x>=cover[1]&&x<cover[3]&&y>=cover[2]&&y<cover[4]){
                    if(cover[0]==a[0]){*px=x;*py=y;return 1;}
                    x=cover[3];break;
                }
            }
            if(i==index){*px=x;*py=y;return 1;}
        }
    }
    return 0;
}
static void area_prepare(KBootstrap *b){
    b->area_nav_count=0;
    for(unsigned i=0;i<b->area_count;i++){
        unsigned j=0;for(;j<b->area_nav_count;j++)if(b->areas[b->area_nav[j][0]][0]==b->areas[i][0])break;
        int x,y;if(j<b->area_nav_count||!area_point(b,i,&x,&y))continue;
        int32_t *target=b->area_nav[b->area_nav_count++];target[0]=(int32_t)i;target[1]=x;target[2]=y;
    }
}
static void area_move(KBootstrap *b,int delta){
    if(!delta||!b->area_nav_count)return;
    unsigned next=0;
    if(b->area_selected>=0){
        unsigned i=0;for(;i<b->area_nav_count;i++)if(b->areas[b->area_nav[i][0]][0]==b->areas[b->area_selected][0])break;
        if(i<b->area_nav_count)next=(i+(delta>0?1:b->area_nav_count-1))%b->area_nav_count;
    }
    const int32_t *target=b->area_nav[next];b->area_selected=target[0];b->area_x=target[1];b->area_y=target[2];
}
