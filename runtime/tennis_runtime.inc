/* CTennis modal runtime: 31/610.  The native match runs by itself (no input),
   so the port advances one point per KTENNIS_POINT_MS and pushes the single
   0/1 result when the best-of-five match is decided. */
#ifndef KISAKU_TENNIS_RUNTIME_INCLUDED
#define KISAKU_TENNIS_RUNTIME_INCLUDED

static void ktennis_release_art(KTennis *t){
    rmt_free(&t->background);rmt_free(&t->player1);rmt_free(&t->court1);rmt_free(&t->court2);
    rmt_free(&t->player2);rmt_free(&t->pieces);
}
static void ktennis_rally_start(KTennis *t);

static unsigned ktennis_difficulty(KBootstrap *b) {
    int difficulty = option(b, "Difficult", "AllMiniGame", 1);   /* 0x4647c0 */
    if (difficulty < 0) difficulty = 0;
    if (difficulty > 2) difficulty = 2;
    return (unsigned)difficulty;
}

static int ktennis_begin(KBootstrap *b) {
    if (b->tennis.active) return error(b, "31/610 already running (arguments preserved)");
    KTennis *t = &b->tennis;
    ktennis_release_art(t);
    *t = (KTennis){0};
    t->difficulty = ktennis_difficulty(b);
    if(mini_load_akb(b,"tennisbg.akb",&t->background)||mini_load_akb(b,"tennisp1.akb",&t->player1)||mini_load_akb(b,"tennisc1.akb",&t->court1)||
       mini_load_akb(b,"tennisc2.akb",&t->court2)||mini_load_akb(b,"tennisp2.akb",&t->player2)||
       mini_load_akb(b,"tennis_p.akb",&t->pieces)){ktennis_release_art(t);return error(b,"31/610 sprite resources decode failed (arguments preserved)");}
    t->active = 1;
    ktennis_rally_start(t);
    return 0;
}

static int ktennis_active(const KBootstrap *b) { return b->tennis.active; }

/* Confirm skips the rally (the native keeps no interactive input; this is the
   port's fast-forward for length reasons and is documented as such). */
static int ktennis_confirm(KBootstrap *b) { b->tennis.fast = 1; return 0; }
static int ktennis_pointer(KBootstrap *b, int x, int y, unsigned held) { (void)b; (void)x; (void)y; (void)held; return 0; }

static void ktennis_rally_start(KTennis *t) {
    /* The native path is driven by CTennisBall, not by the VM RNG.  The
       landing slot remains stable while 20 ms ball updates run. */
    t->rally_code=(t->points_played+t->server+t->difficulty*2u)%6u;
    t->rally_clock=0;
    t->rally_active=1;
}

static void ktennis_point(KBootstrap *b, KTennis *t) {
    /* Player point probability falls as the configured difficulty rises
       (d0 0.55, d1 0.50, d2 0.45); the ball trajectory tables in the native
       are 30.0/32.0/40.0 for difficulty 0/1/2. */
    unsigned roll = kkuji_rand(&b->vm->random_state, 100);
    unsigned threshold = t->difficulty == 0 ? 55u : t->difficulty == 1 ? 50u : 45u;
    int player_point = roll < threshold;
    if (player_point) t->points_player++; else t->points_cpu++;
    t->points_played++;
    unsigned a = t->points_player, c = t->points_cpu;
    t->hud_clock = KTENNIS_HUD_MS;
    /* 0x463d8e..0x463e52: 00/15/30/40, then advantage.  Returning
       from advantage to a tie resets both counters to 3, bounding the atlas
       index.  Unsigned subtraction must not decide a losing side's game. */
    if (a == c && a >= KTENNIS_POINTS_TO_WIN) {
        t->points_player = t->points_cpu = 3;
        t->hud_mode = 1;
        return;
    }
    int game_over = (a >= KTENNIS_POINTS_TO_WIN && a >= c + 2) ||
                    (c >= KTENNIS_POINTS_TO_WIN && c >= a + 2);
    t->hud_mode = a >= 4 || c >= 4 ? 2 : 1;
    if (!game_over) return;
    if (a > c) t->games_player++; else t->games_cpu++;
    t->points_player = t->points_cpu = 0;
    t->server ^= 1u;                     /* native flips [+0x104] each game */
    t->hud_mode = 3;
    if (t->games_player >= KTENNIS_GAMES_TO_WIN || t->games_cpu >= KTENNIS_GAMES_TO_WIN)
        t->hud_clock = 0;                /* 0x463e9c skips the final games panel */
}

static int ktennis_frame(KBootstrap *b, unsigned ms) {
    KTennis *t = &b->tennis;
    if (!t->active) return 0;
    if (t->hud_clock) {
        unsigned elapsed = t->fast ? ms * 8 : ms;
        if (elapsed < t->hud_clock) { t->hud_clock -= elapsed; return 0; }
        t->hud_clock = t->hud_mode = 0;
        return 0;
    }
    unsigned elapsed = t->fast ? ms * KTENNIS_POINT_MS / KTENNIS_FASTFORWARD_MS : ms;
    t->rally_clock += elapsed;
    while (t->rally_clock >= KTENNIS_POINT_MS) {
        t->rally_clock -= KTENNIS_POINT_MS;
        t->rally_active=0;
        ktennis_point(b, t);
        if (t->games_player >= KTENNIS_GAMES_TO_WIN || t->games_cpu >= KTENNIS_GAMES_TO_WIN) break;
        ktennis_rally_start(t);
    }
    if (t->games_player < KTENNIS_GAMES_TO_WIN && t->games_cpu < KTENNIS_GAMES_TO_WIN) return 0;
    if (t->settled) return 0;
    t->result = t->games_player > t->games_cpu ? 1u : 0u;
    t->settled = 1;
    if (kvm_push(b->vm, (KValue){(int32_t)t->result, NULL})) {
        t->active = 0;
        ktennis_release_art(t);
        return error(b, "31/610 result push failed");
    }
    t->active = 0;
    ktennis_release_art(t);
    return 0;
}

static void ktennis_present_hud(KBootstrap *b, const KTennis *t) {
    if (!t->hud_clock || !t->player1.pixels) return;
    /* Sprite +0xd4 at (60,120), 240x114 (0x463803).  All following
       local rectangles are copied by 0x462830 from tennisp1.akb. */
    KImage *dst = &b->layers[0];
    const KImage *src = &t->player1;
    mini_blit_key(dst,src,60,120,0,60,240,114,0x00ff00);
    if (t->hud_mode == 3) {
        mini_blit_key(dst,src,76,146,0,354,208,60,0x00ff00);
        mini_blit_key(dst,src,90,180,208+(int)t->games_player*70,374,70,40,0x00ff00);
        mini_blit_key(dst,src,200,180,208+(int)t->games_cpu*70,374,70,40,0x00ff00);
    } else if (t->hud_mode == 2) {
        mini_blit_key(dst,src,76,146,208,234,208,60,0x00ff00);
        mini_blit_key(dst,src,120,180,208,t->points_player>t->points_cpu?294:334,120,40,0x00ff00);
    } else if (t->points_player == t->points_cpu && t->points_player >= 3) {
        mini_blit_key(dst,src,76,146,208,174,208,60,0x00ff00);
    } else {
        mini_blit_key(dst,src,76,146,0,354,208,60,0x00ff00);
        mini_blit_key(dst,src,90,180,(int)t->points_player*70,414,70,40,0x00ff00);
        mini_blit_key(dst,src,200,180,(int)t->points_cpu*70,414,70,40,0x00ff00);
    }
}

static void ktennis_present(KBootstrap *b) {
    KTennis *t = &b->tennis;
    if (!t->active || !b->layers[0].pixels) return;
    mini_clear(&b->layers[0]);
    mini_blit(&b->layers[0],&t->background,0,0,0,0,640,480);
    if(b->layers[0].width==640&&b->layers[0].height>=480){
        /* The native places one character on each side of the net.  The
           source cells at 0x463764/0x463664: c1=136x160, c2=112x120. */
        unsigned animation_tick=t->points_played*30u+(t->rally_clock/KTENNIS_RALLY_TICK_MS);
        unsigned f=animation_tick%6u;
        unsigned r1=(animation_tick/6u)%8u,r2=r1;
        if(t->court1.pixels)mini_blit_key(&b->layers[0],&t->court1,122,268,(int)f*136,(int)r1*160,136,160,0x00ff00);
        if(t->court2.pixels)mini_blit_key(&b->layers[0],&t->court2,406,116,(int)f*112,(int)r2*120,112,120,0x00ff00);
        if(t->pieces.pixels&&t->rally_active){
            const KTennisLanding *land=&ktennis_landing[t->rally_code%6u];
            int progress=(int)(t->rally_clock>KTENNIS_POINT_MS?KTENNIS_POINT_MS:t->rally_clock);
            int x=180+(270*progress)/KTENNIS_POINT_MS;
            int y=326-(150*progress)/KTENNIS_POINT_MS;
            x+=(land->x1000*progress)/(KTENNIS_POINT_MS*1000/8);
            y+=(land->y1000*progress)/(KTENNIS_POINT_MS*1000/8);
            int arc=(land->h1000*4*progress*(KTENNIS_POINT_MS-progress))/(KTENNIS_POINT_MS*KTENNIS_POINT_MS*1000);
            y-=arc;
            unsigned frame=(t->rally_clock/80u)%3u;
            mini_blit_key(&b->layers[0],&t->pieces,x-11,y-11,0,(int)frame*22,22,22,0x00ff00);
        }
    }
    ktennis_present_hud(b,t);
}
#endif
