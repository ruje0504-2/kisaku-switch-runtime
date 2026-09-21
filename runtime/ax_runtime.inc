/* CFuncAnime 4fee60 / CFuncAnimeEx 5034f0 use separate managers. */
static void ax_unregister(uint32_t counts[AX_CELLS],unsigned index){
    if(index==AX_CELLS)memset(counts,0,AX_CELLS*sizeof(*counts));
    else if(counts[index])counts[index]--;
}
static int ax_manager_wait(KBootstrap *b,struct ax_player *player,unsigned *modal,
                           uint32_t counts[AX_CELLS],unsigned *remove){
    if(*modal==1){
        if(ax_waiting(player))return 1;
        player->wait_cell=0;*modal=0;
    }
    if(*modal==2){
        /* 4de850: request a boundary pause, then wait for every track.
           With animation disabled, pending pauses settle without a tick. */
        for(unsigned i=0;i<AX_CELLS;i++){
            struct ax_cell *c=&player->cells[i];
            if(!c->state){c->state=3;return 1;}
            if(b->vm->globals[0][50].number&0x10){if(c->state!=AX_STOPPED&&c->state!=4)return 1;}
            else if(c->state==3)c->state=4;
        }
        if(*remove){ax_unregister(counts,*remove-1);*remove=0;}
        *modal=0;
    }
    return 0;
}
static int ax_modal_wait(KBootstrap *b){
    int normal=ax_manager_wait(b,&b->ax,&b->ax_modal,b->ax_registered,&b->ax_pause_remove);
    int extra=ax_manager_wait(b,&b->ax_extra,&b->ax_extra_modal,b->ax_extra_registered,&b->ax_extra_pause_remove);
    return normal||extra;
}
static int ax_script_command(KBootstrap *b,int sub){
    if(sub==12){
        /* CFuncAnime virtual_92 -> 4fe8d0: bank, cell, temporary target.
           All three are popped in that order; 4ddcc0 draws the first descriptor. */
        KVM *v=b->vm;
        if(v->sp<4)return error(b,"13/12 operands missing (arguments preserved)");
        for(unsigned i=2;i<=4;i++)if(v->stack[v->sp-i].string)
            return error(b,"13/12 numeric operands required (arguments preserved)");
        int bank=v->stack[v->sp-2].number,cell=v->stack[v->sp-3].number,target=v->stack[v->sp-4].number;
        KImage *dst=surface(b,target);
        if(bank<0||bank>=10||cell<0||cell>=32||!dst||!dst->pixels)
            return error(b,"13/12 track or target invalid (arguments preserved)");
        b->ax_destination=target;
        int drawn=ax_first_frame(&b->ax,(unsigned)bank*32u+(unsigned)cell,draw_ax,b);
        b->ax_destination=0;
        if(!drawn)return error(b,"13/12 first frame invalid (arguments preserved)");
        if(b->error[0])return -1;
        v->sp-=3;return 0;
    }
    if(sub<1||sub>11)return error(b,"AX script command unsupported (arguments preserved)");
    KVM *v=b->vm;
    if(sub==6||sub==7||sub==10||sub==11){
        if(sub==11)for(unsigned i=0;i<AX_CELLS;i++)
            if(b->ax.cells[i].state==4&&b->ax_registered[i]==UINT32_MAX)
                return error(b,"AX registration overflow (arguments preserved)");
        for(unsigned i=0;i<AX_CELLS;i++){
            struct ax_cell *c=&b->ax.cells[i];
            if(sub==6&&c->state!=AX_STOPPED)c->state=1;
            if(sub==7)c->state=AX_STOPPED;
            if(sub==10&&!c->state)c->state=3;
            if(sub==11&&c->state==4){c->state=0;b->ax_registered[i]++;}
        }
        if(sub==6||sub==7)ax_unregister(b->ax_registered,AX_CELLS);
        if(sub==7){b->ax.wait_cell=0;b->ax_modal=0;b->ax_pause_remove=0;}
        if(sub==10){b->ax_modal=2;b->ax_pause_remove=AX_CELLS+1;}
    }else{
        if(v->sp<3||v->stack[v->sp-2].string||v->stack[v->sp-3].string)
            return error(b,"AX numeric track values required (arguments preserved)");
        int32_t bank=v->stack[v->sp-2].number,cell=v->stack[v->sp-3].number;
        if(bank<0||bank>=10||cell<0||cell>=32)return error(b,"AX script track range (arguments preserved)");
        unsigned index=(unsigned)bank*32+(unsigned)cell;struct ax_cell *c=&b->ax.cells[index];
        if((sub==2||(sub==9&&c->state==4))&&b->ax_registered[index]==UINT32_MAX)
            return error(b,"AX registration overflow (arguments preserved)");
        if(sub==2||sub==5){
            if(c->start<0x500||c->start>=b->ax.size||c->ip>=b->ax.size-c->start)
                return error(b,"AX script start invalid (arguments preserved)");
            c->state=0;
        }
        if(sub==1){uint32_t start=c->start;memset(c,0,sizeof(*c));c->start=start;c->state=AX_STOPPED;}
        if(sub==2)b->ax_registered[index]++;
        if(sub==3||sub==4){c->state=sub==3?1:AX_STOPPED;ax_unregister(b->ax_registered,index);}
        if(sub==5){b->ax.wait_cell=index+1;b->ax_modal=1;}
        if(sub==8){if(!c->state)c->state=3;b->ax_modal=2;b->ax_pause_remove=index+1;}
        if(sub==9&&c->state==4){c->state=0;b->ax_registered[index]++;}
        v->sp-=2;
    }
    for(unsigned i=0;i<AX_CELLS;i++)b->animation_status[i]=(uint8_t)b->ax.cells[i].state;
    return 0;
}
static int ax_modal_skip(KBootstrap *b,int cancel){
    if(!b->ax_modal&&!b->ax_extra_modal)return 0;
    /* 4deb80 tests flag 0x20 before accepting input mask 0x101.
       Pause barriers never accept input; consume it without affecting UI. */
    if(!cancel||!(b->vm->globals[0][50].number&0x20))return 1;
    struct ax_player *players[2]={&b->ax,&b->ax_extra};
    unsigned *modals[2]={&b->ax_modal,&b->ax_extra_modal};
    for(unsigned i=0;i<2;i++)if(*modals[i]==1){
        if(players[i]->wait_cell)players[i]->cells[players[i]->wait_cell-1].state=AX_STOPPED;
        players[i]->wait_cell=0;*modals[i]=0;
    }
    return 1;
}
