/* 31/310: 4fb090; normal 49b2e0/49c5d0, Hage 4a9de0/4ab220.
   This modal owns its screen and a disposable runtime for native CG scripts. */
typedef struct KNativeCG {
    KImage base,screen,parts,atlas,variants;
    unsigned alternate,group,page,view,item,variant;
    int focus;KBootstrap *player;char problem[256];
} KNativeCG;
static const unsigned cg_starts[]={0,22,48,71,112,147,172,195,225,278};
static const unsigned cg_counts[]={21,25,22,40,34,24,22,29,52,28};
static const char *const cg_atlases[]={"kisaku_DL_CG00nadesiko.akb","kisaku_DL_CG01hiromi.akb","kisaku_DL_CG02momoko.akb","kisaku_DL_CG03madoka.akb","kisaku_DL_CG04yuri.akb","kisaku_DL_CG05aoi.akb","kisaku_DL_CG06ayano.akb","kisaku_DL_CG07syouko.akb","kisaku_DL_CG08sonota.akb","kisaku_DL_CG09onnanoko.akb"};
static unsigned cg_count(const KNativeCG *c){return c->alternate?27:cg_counts[c->group];}
static unsigned cg_columns(const KNativeCG *c){return c->alternate?5:4;}
static unsigned cg_pages(const KNativeCG *c){unsigned n=cg_columns(c)*4;return (cg_count(c)+n-1)/n;}
static const KCGRecord *cg_record(const KNativeCG *c,unsigned item){return c->alternate?&hage_cg_records[item]:&kisaku_cg_records[cg_starts[c->group]+item];}
static unsigned cg_variant_count(const KCGRecord *r){unsigned n=0;while(n<30&&r->variants[n]>=0)n++;return n;}
static int cg_unlocked(const KBootstrap *b,int flag){return flag>=0&&(unsigned)flag<b->vm->byte_count&&b->vm->bytes[flag]==1;}
static int cg_item_unlocked(const KBootstrap *b,const KCGRecord *r){
    if(cg_unlocked(b,r->flag))return 1;
    for(unsigned i=0;i<cg_variant_count(r);i++)if(cg_unlocked(b,r->variants[i]))return 1;
    return 0;
}
static int cg_read(KBootstrap *b,const char *name,KImage *out){
    uint8_t *bytes=NULL;size_t size=0;KImage image={0};
    int bad=read_named(&b->images,name,&bytes,&size)||rmt_decode(bytes,size,&image);free(bytes);
    if(bad){rmt_free(&image);return -1;}rmt_free(out);*out=image;return 0;
}
static int cg_copy(KImage *dst,const KImage *src,int dx,int dy,int sx,int sy,int w,int h){
    if(!src->pixels||sx<0||sy<0||sx+w>(int)src->width||sy+h>(int)src->height)return -1;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        if(dx+x<0||dy+y<0||dx+x>=(int)dst->width||dy+y>=(int)dst->height)continue;
        const uint8_t *s=src->pixels+(sy+y)*src->stride+(sx+x)*4;uint8_t *d=dst->pixels+(dy+y)*dst->stride+(dx+x)*4;
        unsigned a=s[3];for(unsigned k=0;k<3;k++)d[k]=(uint8_t)(s[k]*a/255+d[k]*(255-a)/255);d[3]=255;
    }return 0;
}
static void cg_variant_rect(const KCGRecord *r,unsigned i,int *x,int *y){
    unsigned count=cg_variant_count(r),cols=count<5?count:5,rows=(count+4)/5;
    *x=(640-(int)(cols*108+(cols-1)*8))/2+(int)(i%5)*116;
    *y=(480-(int)(rows*84+(rows-1)*8))/2+(int)(i/5)*92;
}
static int cg_draw(KBootstrap *b){
    KNativeCG *c=b->native_cg;if(!c)return -1;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *s=c->base.pixels+y*c->base.stride+x*4;uint8_t *d=c->screen.pixels+y*c->screen.stride+x*4;
        for(unsigned k=0;k<3;k++)d[k]=(uint8_t)(s[k]*127/255);
        d[3]=255;
    }
    if(c->view==2&&c->player){return cg_copy(&c->screen,&c->player->layers[0],0,0,0,0,640,480);}
    if(c->view==1){
        const KCGRecord *r=cg_record(c,c->item);
        for(unsigned i=0;i<cg_variant_count(r);i++){
            int x,y;cg_variant_rect(r,i,&x,&y);
            if(cg_copy(&c->screen,&c->parts,x+4,y+4,112,28,100,76))return -1;
            if(cg_unlocked(b,r->variants[i])&&cg_copy(&c->screen,&c->variants,x+4,y+4,(int)(i%6)*100,(int)(i/6)*76,100,76))return -1;
            if(cg_copy(&c->screen,&c->parts,x,y,212,c->focus==(int)i&&cg_unlocked(b,r->variants[i])?84:0,108,84))return -1;
        }return 0;
    }
    if(cg_copy(&c->screen,&c->parts,2,2,0,0,116,28)||cg_copy(&c->screen,&c->parts,612,0,480,184,28,20)||
       cg_copy(&c->screen,&c->parts,528,448,0,c->focus==0x200?60:28,112,32))return -1;
    unsigned pages=cg_pages(c);
    for(unsigned i=0;i<pages;i++)if(cg_copy(&c->screen,&c->parts,(10-(int)pages)*38+232+(int)i*38,0,(int)i*48,i==c->page?224:c->focus==(int)(0x100+i)?204:184,48,20))return -1;
    if(!c->alternate)for(unsigned i=0;i<10;i++){
        int sx=kisaku_cg_tabs[i][0],sy=kisaku_cg_tabs[i][1];
        if(i==9&&!b->vm->bytes[4006]){sx=620;sy=380;}else if(i==c->group)sy+=40;else if(c->focus==(int)(0x300+i))sy+=20;
        if(cg_copy(&c->screen,&c->parts,0,200+(int)i*28,sx,sy,112,20))return -1;
    }
    unsigned cols=cg_columns(c),first=c->page*cols*4;
    for(unsigned i=0;i<cols*4&&first+i<cg_count(c);i++){
        const KCGRecord *r=cg_record(c,first+i);int x=(c->alternate?38:154)+(int)(i%cols)*116,y=52+(int)(i/cols)*96;
        if(cg_copy(&c->screen,&c->parts,x,y,112,28,100,76))return -1;
        if(cg_unlocked(b,r->flag)){
            if(cg_copy(&c->screen,&c->atlas,x,y,(int)((first+i)%6)*100,(int)((first+i)/6)*76,100,76))return -1;
        }else if(!c->alternate&&cg_item_unlocked(b,r)){
            KImage fallback={0};unsigned v=0;while(v<cg_variant_count(r)&&!cg_unlocked(b,r->variants[v]))v++;
            int bad=cg_read(b,r->atlas,&fallback)||cg_copy(&c->screen,&fallback,x,y,(int)(v%6)*100,(int)(v/6)*76,100,76);rmt_free(&fallback);if(bad)return -1;
        }
        if(cg_copy(&c->screen,&c->parts,x-4,y-4,212,c->focus==(int)i&&cg_item_unlocked(b,r)?84:0,108,84))return -1;
        unsigned total=cg_variant_count(r),seen=0;for(unsigned v=0;v<total;v++)seen+=cg_unlocked(b,r->variants[v]);
        if(total!=1&&cg_item_unlocked(b,r)){
            if(seen>=10&&cg_copy(&c->screen,&c->parts,x+58,y+64,124+(int)(seen/10)*8,0,8,12))return -1;
            if(cg_copy(&c->screen,&c->parts,x+66,y+64,124+(int)(seen%10)*8,0,8,12)||cg_copy(&c->screen,&c->parts,x+74,y+64,116,0,8,12)||
               (total>=10&&cg_copy(&c->screen,&c->parts,x+82,y+64,124+(int)(total/10)*8,0,8,12))||cg_copy(&c->screen,&c->parts,x+90,y+64,124+(int)(total%10)*8,0,8,12))return -1;
        }
    }return 0;
}
static void native_cg_free(KBootstrap *b){
    KNativeCG *c=b->native_cg;if(!c)return;b->native_cg=NULL;
    bootstrap_destroy(c->player);rmt_free(&c->base);rmt_free(&c->screen);rmt_free(&c->parts);rmt_free(&c->atlas);rmt_free(&c->variants);free(c);
}
static int native_cg_open(KBootstrap *b){
    if(b->native_cg||b->message_request||b->vm->global_count[1]<=61||b->layers[0].width<640||b->layers[0].height<480)return -1;
    KNativeCG *c=calloc(1,sizeof(*c));if(!c)return -1;c->alternate=b->vm->globals[1][61].number==1;
    c->base=(KImage){0,0,640,480,2560,malloc(640*480*4)};c->screen=(KImage){0,0,640,480,2560,malloc(640*480*4)};
    b->native_cg=c;
    if(!c->base.pixels||!c->screen.pixels||cg_read(b,"kisaku_DL_CGmode_p.akb",&c->parts)||cg_read(b,c->alternate?"hage_DL_CGmode_p.akb":cg_atlases[0],&c->atlas))goto bad;
    for(unsigned y=0;y<480;y++)memcpy(c->base.pixels+y*2560,b->layers[0].pixels+y*b->layers[0].stride,2560);
    if(cg_draw(b))goto bad;
    b->message_request=22;return 0;
bad:native_cg_free(b);return -1;
}
const KImage *bootstrap_native_cg_image(const KBootstrap *b){return b&&b->native_cg?&b->native_cg->screen:NULL;}
/* 499d60/4a96xx executes original script with sys21=item, sys13=variant.
   A separate fully initialized runtime protects the suspended caller's stack,
   resources and progress from this script interpreter. */
static int cg_play(KBootstrap *b,unsigned variant){
    KNativeCG *c=b->native_cg;const KCGRecord *r=cg_record(c,c->item);
    if(variant>=cg_variant_count(r)||!cg_unlocked(b,r->variants[variant]))return 0;
    KBootstrap *p=bootstrap_create_split(b->root,bootstrap_save_dir(b));if(!p)return -1;
    for(unsigned i=0;i<6000&&!p->title.active;i++){if(bootstrap_run(p,100000)<0)goto bad;bootstrap_frame(p);}
    if(!p->title.active)goto bad;
    for(unsigned bank=0;bank<2;bank++){p->vm->global_count[bank]=b->vm->global_count[bank];for(unsigned i=0;i<p->vm->global_count[bank];i++)if(owned_value(p,&p->vm->globals[bank][i],b->vm->globals[bank][i]))goto bad;}
    memcpy(p->vm->bytes,b->vm->bytes,sizeof(p->vm->bytes));p->vm->byte_count=b->vm->byte_count;
    memcpy(p->vm->words,b->vm->words,sizeof(p->vm->words));p->vm->word_count=b->vm->word_count;
    p->vm->globals[0][21]=(KValue){(int)c->item,NULL};p->vm->globals[0][13]=(KValue){(int)variant,NULL};
    p->vm->globals[0][50].number&=~0x1100;p->title.active=0;p->logo_phase=0;
    int id=module(p,r->script);if(id<0||kvm_start(p->vm,id))goto bad;
    int done=0;for(unsigned i=0;i<6000;i++){int rc=bootstrap_run(p,100000);if(rc<0)goto bad;if(!rc){done=1;break;}bootstrap_frame(p);}
    if(!done)goto bad;
    bootstrap_destroy(c->player);c->player=p;c->variant=variant;c->view=2;return cg_draw(b);
bad:snprintf(c->problem,sizeof(c->problem),"CG script: %.230s",p->error[0]?p->error:"execution limit");bootstrap_destroy(p);return -1;
}
static void native_cg_frame(KBootstrap *b){KNativeCG *c=b->native_cg;if(c&&c->view==2&&c->player){bootstrap_frame(c->player);(void)cg_draw(b);}}
int bootstrap_native_cg_action(KBootstrap *b,int action){
    KNativeCG *c=b?b->native_cg:NULL;if(!c)return -1;
    if(action==1){
        if(c->view){bootstrap_destroy(c->player);c->player=NULL;c->view--;c->focus=c->view?(int)c->variant:(int)(c->item%(cg_columns(c)*4));return cg_draw(b);}
        if(kvm_push(b->vm,(KValue){-1,NULL}))return -1;
        native_cg_free(b);return 0;
    }
    if(c->view==2){
        if(!action||action==5){const KCGRecord *r=cg_record(c,c->item);for(unsigned v=c->variant+1;v<cg_variant_count(r);v++)if(cg_unlocked(b,r->variants[v]))return cg_play(b,v);return bootstrap_native_cg_action(b,1);}
        if(action==4){const KCGRecord *r=cg_record(c,c->item);for(int v=(int)c->variant-1;v>=0;v--)if(cg_unlocked(b,r->variants[v]))return cg_play(b,(unsigned)v);}return 0;
    }
    if((action==6||action==7)&&!c->view&&!c->alternate){
        unsigned groups=b->vm->bytes[4006]?10:9;
        unsigned group=(c->group+groups+(action==7?-1:1))%groups;
        KImage next={0};if(cg_read(b,cg_atlases[group],&next))return -1;
        rmt_free(&c->atlas);c->atlas=next;c->group=group;c->page=0;c->focus=0;
        return cg_draw(b);
    }
    if(!action){
        if(c->view)return cg_play(b,(unsigned)c->focus);
        if(c->focus==0x200)return bootstrap_native_cg_action(b,1);
        if(c->focus>=0x300&&c->focus<0x30a){
            unsigned group=(unsigned)c->focus-0x300;if(group==9&&!b->vm->bytes[4006])return 0;
            KImage next={0};if(cg_read(b,cg_atlases[group],&next))return -1;rmt_free(&c->atlas);c->atlas=next;c->group=group;c->page=0;
        }else if(c->focus>=0x100&&c->focus<0x100+(int)cg_pages(c))c->page=(unsigned)c->focus-0x100;
        else if(c->focus>=0&&c->focus<(int)(cg_columns(c)*4)){
            unsigned item=c->page*cg_columns(c)*4+(unsigned)c->focus;if(item>=cg_count(c))return 0;
            const KCGRecord *r=cg_record(c,item);if(!cg_item_unlocked(b,r))return 0;
            if(cg_read(b,r->atlas,&c->variants))return -1;
            c->item=item;c->view=1;c->focus=0;
        }
    }else if(action==8||action==9){if(!c->view)c->page=(c->page+cg_pages(c)+(action==8?-1:1))%cg_pages(c);}
    else if(action>=2&&action<=5){
        int cols=c->view?5:(int)cg_columns(c),count=c->view?(int)cg_variant_count(cg_record(c,c->item)):(int)cg_count(c)-(int)(c->page*cg_columns(c)*4);
        if(!c->view&&count>cols*4)count=cols*4;
        if(c->focus>=0x300){if(action==2&&c->focus>0x300)c->focus--;else if(action==3&&c->focus<0x309)c->focus++;else if(action==5)c->focus=0;}
        else if(c->focus>=0x100){if(action==3)c->focus=0;else if(action==4&&!c->alternate)c->focus=0x300;else if(action==2)c->focus=count-1;}
        else {int next=c->focus+(action==2?-cols:action==3?cols:action==4?-1:1);if(next<0)c->focus=!c->view?(action==4&&!c->alternate?0x300:0x100):0;else if(next>=count)c->focus=c->view?count-1:0x200;else c->focus=next;}
    }return cg_draw(b);
}
int bootstrap_native_cg_pointer(KBootstrap *b,int x,int y,int click){
    KNativeCG *c=b?b->native_cg:NULL;if(!c)return -1;if(click==2)return bootstrap_native_cg_action(b,1);
    if(c->view==2)return click?bootstrap_native_cg_action(b,0):0;
    int hit=-1;
    if(c->view){const KCGRecord *r=cg_record(c,c->item);for(unsigned i=0;i<cg_variant_count(r);i++){int rx,ry;cg_variant_rect(r,i,&rx,&ry);if(x>=rx&&x<rx+108&&y>=ry&&y<ry+84)hit=(int)i;}}
    else {
        unsigned cols=cg_columns(c);int left=c->alternate?34:150;
        for(unsigned i=0;i<cols*4&&c->page*cols*4+i<cg_count(c);i++){int rx=left+(int)(i%cols)*116,ry=48+(int)(i/cols)*96;if(x>=rx&&x<rx+108&&y>=ry&&y<ry+84)hit=(int)i;}
        if(x>=528&&y>=448)hit=0x200;
        if(!c->alternate&&x>=0&&x<112&&y>=200&&y<472&&(y-200)%28<20)hit=0x300+(y-200)/28;
        for(unsigned i=0;i<cg_pages(c);i++){int rx=(10-(int)cg_pages(c))*38+232+(int)i*38;if(y>=0&&y<20&&x>=rx&&x<rx+48)hit=0x100+(int)i;}
    }
    if(hit>=0){c->focus=hit;if(click)return bootstrap_native_cg_action(b,0);return cg_draw(b);}return 0;
}

/* Presentation-only focus bounds; never paint accessibility markers into the
   VM's image surfaces or saved thumbnails. Returns 0 while viewing a CG. */
int bootstrap_native_cg_focus(const KBootstrap *b,int rect[4]){
    const KNativeCG *c=b?b->native_cg:NULL;
    if(!c||c->view==2||c->focus<0)return 0;
    int x,y,w,h,f=c->focus;
    if(c->view){if((unsigned)f>=cg_variant_count(cg_record(c,c->item)))return 0;cg_variant_rect(cg_record(c,c->item),(unsigned)f,&x,&y);w=108;h=84;}
    else if(f>=0x300){x=0;y=200+(f-0x300)*28;w=112;h=20;}
    else if(f==0x200){x=528;y=448;w=112;h=32;}
    else if(f>=0x100){x=(10-(int)cg_pages(c))*38+232+(f-0x100)*38;y=0;w=48;h=20;}
    else{x=(c->alternate?34:150)+(f%(int)cg_columns(c))*116;y=48+(f/(int)cg_columns(c))*96;w=108;h=84;}
    rect[0]=x;rect[1]=y;rect[2]=w;rect[3]=h;return 1;
}

unsigned bootstrap_native_cg_categories(const KBootstrap *b){
    const KNativeCG *c=b?b->native_cg:NULL;
    return c&&!c->view&&!c->alternate?(b->vm->bytes[4006]?10:9):0;
}
