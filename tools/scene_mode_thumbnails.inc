/* CScMode 470450 deletes a column and packs subsequent frames left.
   Rebuild once per page from immutable AKB input; never alter script layers. */
static void scene_thumb_delete(KImage *im,unsigned row,unsigned col){
    if(!im->pixels||row*112+112>im->height||im->width<596||col>=4)return;
    for(unsigned y=row*112;y<(row+1)*112;y++){
        uint8_t *p=im->pixels+y*im->stride+col*149*4;
        memmove(p,p+149*4,(3-col)*149*4);
        p=im->pixels+y*im->stride+3*149*4;
        for(unsigned x=0;x<149;x++){p[x*4]=p[x*4+1]=p[x*4+2]=0;p[x*4+3]=255;}
    }
}
static unsigned scene_thumb_count(const KImage *im,unsigned row){
    if(!im->pixels||row*112+112>im->height||im->width<596)return 0;
    for(unsigned col=0;col<4;col++){
        int present=0;
        for(unsigned y=0;y<112&&!present;y++)for(unsigned x=0;x<149;x++){
            const uint8_t *q=im->pixels+(row*112+y)*im->stride+(col*149+x)*4;
            if(q[0]||q[1]||q[2]||q[3]!=255){present=1;break;}
        }
        if(!present)return col;
    }return 4;
}
static int scene_thumb_extra(KBootstrap *b,KImage *im,unsigned cat,unsigned page,unsigned slot){
    char name[64];snprintf(name,sizeof(name),"scene_%02u_%02u_%02u.akb",kscene_mode_resource_group[cat],page,slot);
    rmt_free(im);return ui_akb_asset(b,name,im);
}
/* 470ba0: conditional thumbnail substitutions, including the native maximum
   (4201d0, not bitwise OR) used by the aggregate variant selectors. */
static int scene_thumb_reorder(MessagePanel *p,KBootstrap *b,unsigned cat,unsigned page){
    unsigned v[9];for(unsigned i=0;i<9;i++)v[i]=kscene_mode_flag_value(b->vm->bytes,b->vm->byte_count,kscene_mode_flags[cat][page*9+i]);
    KImage *im=&p->direct_thumb,extra={0};int bad=0;
#define DEL(row,col) scene_thumb_delete(im,row,col)
#define BYTE(id) kscene_mode_flag_value(b->vm->bytes,b->vm->byte_count,id)
#define LOAD(slot) do{if(scene_thumb_extra(b,&extra,cat,page,slot)){bad=-1;goto done;}}while(0)
#define TILE(dst,col,row,src) ui_blit(im,&extra,(src)*149,(row)*112,149,112,(col)*149,(dst)*112,0)
    if(cat==1&&page==0&&v[0])DEL(0,v[0]==1?1:0);
    else if(cat==2&&page==0&&v[4])DEL(4,v[4]==1?2:3);
    else if(cat==4&&page==0){
        if(v[0]){if(!(v[0]&1))DEL(0,3);if(!(v[0]&2))DEL(0,2);}
        if(v[1]){if(!BYTE(3596)){DEL(1,3);DEL(1,2);}else DEL(1,1);}
        if(v[5]){if(v[5]<5){DEL(5,3);DEL(5,2);}else{DEL(5,1);DEL(5,0);}}
    }else if(cat==6&&page==0&&v[1]){if(!(v[1]&4))DEL(1,3);if(!(v[1]&2))DEL(1,2);if(!(v[1]&1))DEL(1,1);}
    else if(cat==7&&page==0&&v[2]&&!BYTE(3598))DEL(2,2);
    else if(cat==8&&page==0){
        if(v[5]){if(!(v[5]&1)){DEL(5,3);DEL(5,2);}if(!(v[5]&2))DEL(5,1);}
        if(v[7]&&v[7]!=1){LOAD(7);if(v[7]&4)ui_blit(im,&extra,0,112,596,112,0,7*112,0);else if(v[7]&2)ui_blit(im,&extra,0,0,596,112,0,7*112,0);else TILE(7,0,0,0);}
        if(v[8]){
            unsigned chosen=0;
            for(unsigned flag=3459;flag<=3465;flag++){
                unsigned value=BYTE(flag);if(!value)continue;value--;unsigned candidate;
                if(flag<=3461){candidate=flag==3459?4:1;if(!(value&2))candidate=5;if(!(value&1))candidate|=2;}
                else candidate=(~value)&7;
                if(candidate>chosen)chosen=candidate;
            }
            LOAD(8);TILE(8,0,chosen<4?7:8,chosen%4);
        }
    }else if(cat==8&&page==1){
        if(v[1]){
            unsigned chosen=0;
            for(unsigned flag=3476;flag<=3480;flag++){
                unsigned value=BYTE(flag),candidate;
                switch(value){case 1:case 2:case 7:case 8:case 13:case 14:candidate=0;break;case 3:case 9:case 15:candidate=1;break;case 4:case 5:case 10:case 11:case 16:case 17:candidate=2;break;default:candidate=3;break;}
                if(candidate>chosen)chosen=candidate;
            }
            if(chosen!=3){LOAD(1);TILE(1,0,chosen==0?5:chosen==1?7:6,0);}
        }
        /* Original 4713xx uses row3's progress to prune row5. */
        if(v[3]){if(!(v[3]&4))DEL(5,3);if(!(v[3]&2))DEL(5,2);}
        if(v[4]){
            unsigned keep=BYTE(3489),value=BYTE(3486);if(!keep&&(value==1||value==3))keep=1;
            if(!keep&&BYTE(3487)==1)keep=1;
            value=BYTE(3488);if(value==1||value==3||value==5)keep=1;
            if(!keep&&BYTE(3490)==1)keep=1;
            if(!keep&&BYTE(3491)==1)keep=1;
            value=BYTE(3492);if(!keep&&(value==1||value==3))keep=1;
            if(!keep&&BYTE(3493)==2)keep=1;
            if(!keep&&BYTE(3494)==1)keep=1;
            DEL(4,keep?1:0);
        }
    }else if(cat==9&&page==0&&v[8]){if(!BYTE(3512)&&!BYTE(3513))DEL(8,3);if(!BYTE(3511))DEL(8,2);}
 done:
    rmt_free(&extra);
#undef DEL
#undef BYTE
#undef LOAD
#undef TILE
    return bad;
}
