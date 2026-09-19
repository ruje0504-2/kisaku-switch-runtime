/* 42cae0 -> 48bdd0: main ending credits, not the configuration dialog.
   Page 5 is two 640x2000 columns. One pixel per 32 ms, with the next
   column filling the bottom of the screen before the first runs out. */
static void credits_finish(KBootstrap *b,int skipped){
    b->credits_active=0;
    if(kvm_push(b->vm,(KValue){skipped,NULL}))error(b,"credits result overflow");
}
static void credits_draw(KBootstrap *b){
    KImage *atlas=&b->layers[5],*screen=&b->layers[0];
    for(unsigned y=0;y<480;y++){
        int row=(int)(b->credits_row+y)-479;
        if(row<0)continue; /* Original leaves the unreached background intact. */
        unsigned sx=(unsigned)row/2000*640,sy=(unsigned)row%2000;
        memcpy(screen->pixels+y*screen->stride,atlas->pixels+sy*atlas->stride+sx*4,640*4);
    }
}
static int credits_begin(KBootstrap *b){
    if(!b->layers[5].pixels||b->layers[5].width<1280||b->layers[5].height<2000)return error(b,"ending credits atlas missing");
    b->credits_active=1;b->credits_row=0;b->credits_clock=0;credits_draw(b);return 0;
}
static void credits_frame(KBootstrap *b){
    b->credits_clock+=1000;
    if(b->credits_clock<1920)return;
    b->credits_clock-=1920;b->credits_row++;
    if(b->credits_row==3913){credits_finish(b,0);return;}
    credits_draw(b);
}
