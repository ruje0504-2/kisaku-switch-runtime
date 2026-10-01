/* CKuji modal runtime: logic-first slice of 31/710.  KKuji owns no heap
 * memory, so there is nothing to release at shutdown. */
/* CKuji modal runtime: logic-first slice of 31/710.
 *
 * The native handler is synchronous: it runs an interactive modal
 * (ISelect::run through the inherited vtable slot 0x68) until the player
 * confirms a column, then pushes the resolved prize and returns.  The port
 * drives the same shape as the bowling runtime: the dispatch sets the modal
 * up, bootstrap_run() reports "busy" while kkuji_active() is set, and the
 * result is pushed exactly once when the walk finishes. */
#ifndef KISAKU_KUJI_RUNTIME_INCLUDED
#define KISAKU_KUJI_RUNTIME_INCLUDED

static void kkuji_release_art(KKuji *k){
    rmt_free(&k->background);rmt_free(&k->prompt);rmt_free(&k->cursor);
    for(unsigned i=0;i<KKUJI_COLUMNS;i++)rmt_free(&k->pages[i]);
}

typedef struct { int x,y; } KKujiPoint;

/* The five kujip pages are the authored red routes.  Their horizontal and
 * vertical runs are deliberately kept as data: this is the 3-pixel-per-step
 * path used by CKuji, and also prevents a guessed straight-line interpolation
 * from moving the player through a rung that is not present in the artwork. */
static const KKujiPoint kkuji_paths[KKUJI_COLUMNS][14] = {
    {{78,110},{78,180},{215,180},{215,220},{321,220},{321,260},{445,260},
     {445,348},{570,348},{570,460},{445,460},{445,550},{321,550},{321,594}},
    {{215,115},{215,180},{78,180},{78,310},{215,310},{215,356},{321,356},
     {321,408},{445,408},{445,460},{570,460},{570,584},{570,584},{570,584}},
    {{321,110},{321,140},{445,140},{445,216},{570,216},{570,340},{445,340},
     {445,408},{321,408},{321,444},{215,444},{215,496},{78,496},{78,584}},
    {{445,112},{445,140},{321,140},{321,216},{215,216},{215,312},{78,312},
     {78,400},{215,400},{215,444},{321,444},{321,550},{445,550},{445,584}},
    {{570,110},{570,216},{445,216},{445,260},{321,260},{321,356},{215,356},
     {215,400},{78,400},{78,496},{215,496},{215,586},{215,586},{215,586}}
};
static const unsigned kkuji_path_points=14;

static unsigned kkuji_route_length(unsigned route){
    if(route>=KKUJI_COLUMNS)return 0;
    unsigned length=0;
    for(unsigned i=1;i<kkuji_path_points;i++){
        int dx=kkuji_paths[route][i].x-kkuji_paths[route][i-1].x;
        int dy=kkuji_paths[route][i].y-kkuji_paths[route][i-1].y;
        length+=(unsigned)(dx<0?-dx:dx)+(unsigned)(dy<0?-dy:dy);
    }
    return length;
}

static void kkuji_path_position(unsigned route,unsigned distance,int *x,int *y,int *direction){
    if(route>=KKUJI_COLUMNS){*x=*y=0;if(direction)*direction=1;return;}
    const KKujiPoint *p=kkuji_paths[route];
    for(unsigned i=1;i<kkuji_path_points;i++){
        int dx=p[i].x-p[i-1].x,dy=p[i].y-p[i-1].y;
        unsigned length=(unsigned)(dx<0?-dx:dx)+(unsigned)(dy<0?-dy:dy);
        if(distance<=length){
            int sx=dx?((dx>0)?1:-1):0,sy=dy?((dy>0)?1:-1):0;
            *x=p[i-1].x+sx*(int)(dx?distance:0);
            *y=p[i-1].y+sy*(int)(dy?distance:0);
            if(direction)*direction=dx? sx:sy;
            return;
        }
        distance-=length;
    }
    *x=p[kkuji_path_points-1].x;*y=p[kkuji_path_points-1].y;
    if(direction)*direction=1;
}

static int kkuji_begin(KBootstrap *b, const unsigned pool[4]) {
    if (b->kuji.active) return error(b, "31/710 already running (arguments preserved)");
    KKuji *k = &b->kuji;
    kkuji_release_art(k);
    if (mini_load_akb(b,"kuji_bg.akb",&k->background)||
       mini_load_akb(b,"kuji_pt.akb",&k->prompt)||
       mini_load_akb(b,"kuji_c.akb",&k->cursor)){kkuji_release_art(k);return error(b,"31/710 artwork decode failed (arguments preserved)");}
    for(unsigned i=0;i<KKUJI_COLUMNS;i++){
        char name[32];snprintf(name,sizeof(name),"kujip%u.akb",i+1);
        if(mini_load_akb(b,name,&k->pages[i])){kkuji_release_art(k);return error(b,"31/710 prize page decode failed (arguments preserved)");}
    }
    for (unsigned i = 0; i < 4; i++) {
        if (pool[i] > 9){kkuji_release_art(k);return error(b, "31/710 prize id range (arguments preserved)");}
        k->pool[i] = pool[i];
    }
    k->pool[4] = KKUJI_BLANK;
    kkuji_shuffle(k, &b->vm->random_state);
    k->select = 0;
    k->result = KKUJI_BLANK;
    k->chosen = k->drawn = k->clock = 0;
    k->path_length = 0;
    k->cursor_x = k->cursor_y = 0;
    /* The five selectable endpoints occupy the lower part of the logical
       canvas; keep that native selection view visible until the walk starts. */
    k->viewport_y = 256;
    k->active = 1;
    return 0;
}

static int kkuji_active(const KBootstrap *b) { return b->kuji.active; }
static int kkuji_confirm(KBootstrap *b);

/* Pointer hover uses display coordinates.  The native hit rectangles live on
   the 640x736 artwork; translate through the active 640x480 viewport before
   testing them so the bottom five choices are reachable on Switch. */
static int kkuji_pointer(KBootstrap *b, int x, int y, unsigned held) {
    KKuji *k = &b->kuji;
    if (!k->active || k->chosen) return 0;
    for (unsigned i = 0; i < KKUJI_COLUMNS; i++) {
        int cx = kkuji_column_x[i];
        int logical_y=y+(int)k->viewport_y;
        if (x >= cx - 60 && x < cx + 60 && logical_y >= 600 - 98 && logical_y < 600 + 48) {
            k->select = i;
            if (held) kkuji_confirm(b);
            return 1;
        }
    }
    return 0;
}

/* Confirmation (ISelect vtable slot 27 = 0x48f850): start the walk. */
static int kkuji_confirm(KBootstrap *b) {
    KKuji *k = &b->kuji;
    if (!k->active) return 0;
    if (!k->chosen) {
        k->chosen = 1; k->clock = 0;
        k->path_length=kkuji_route_length(k->select);
        kkuji_path_position(k->select,0,&k->cursor_x,&k->cursor_y,NULL);
    }
    return 0;
}

/* Per-frame: after the confirmation the native walks the fixed polyline
   (20 ms per step) and waits one more second before handing the prize back. */
static int kkuji_frame(KBootstrap *b) {
    KKuji *k = &b->kuji;
    if (!k->active) return 0;
    if (!k->chosen) return 0;
    k->clock += KKUJI_STEP_MS;
    if (!k->drawn) k->drawn = 1;      /* kujip{select+1}.akb request point */
    unsigned walk_steps=(k->path_length+2)/3;
    unsigned walk_ms=walk_steps*KKUJI_STEP_MS;
    unsigned distance=(k->clock/KKUJI_STEP_MS)*3;
    if(distance>k->path_length)distance=k->path_length;
    kkuji_path_position(k->select,distance,&k->cursor_x,&k->cursor_y,NULL);
    k->viewport_y=k->cursor_y>240?(unsigned)(k->cursor_y-240):0;
    if(k->viewport_y>256)k->viewport_y=256;
    if (k->clock < walk_ms+KKUJI_TAIL_MS) return 0;
    k->result = k->slot[kkuji_perm[k->select]];
    if (kvm_push(b->vm, (KValue){(int32_t)k->result, NULL})) {
        k->active = 0;
        kkuji_release_art(k);
        return error(b, "31/710 result push failed");
    }
    k->active = 0;
    kkuji_release_art(k);
    return 0;
}

static void kkuji_present(KBootstrap *b) {
    KKuji *k = &b->kuji;
    if (!k->active || !b->layers[0].pixels) return;
    mini_clear(&b->layers[0]);
    if(!k->chosen&&k->prompt.pixels){
        mini_blit_key(&b->layers[0],&k->prompt,0,0,0,0,640,480,0x00ff00);
        return;
    }
    mini_blit(&b->layers[0],&k->background,0,0,0,(int)k->viewport_y,640,480);
    if(k->select<KKUJI_COLUMNS){
        mini_blit(&b->layers[0],&k->pages[k->select],0,0,0,(int)k->viewport_y,640,480);
        if(k->chosen&&k->cursor.pixels){
            unsigned frame=(k->clock/100u)%8u;int direction=1;
            unsigned distance=(k->clock/KKUJI_STEP_MS)*3;
            if(distance>k->path_length)distance=k->path_length;
            kkuji_path_position(k->select,distance,&k->cursor_x,&k->cursor_y,&direction);
            unsigned row=direction<0?1u:0u;
            mini_blit_key(&b->layers[0],&k->cursor,k->cursor_x-60,(int)k->cursor_y-(int)k->viewport_y-80,
                          (int)(frame*120u),(int)(row*96u),120,96,0x00ff00);
        }
    }
}

#endif
