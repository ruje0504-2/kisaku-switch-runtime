/* main13 uses the AX object's API directly, distinct from main28's low-byte
   command dispatcher. 42a4e0 / 42a5f0 is the native 0..11 dispatch table. */
static int ax_modal_wait(KBootstrap *b){
    if(b->ax_modal==1){if(ax_waiting(&b->ax))return 1;b->ax_modal=0;}
    if(b->ax_modal==2){
        /* 40bf60 scans all tracks, requesting a boundary pause as it goes. */
        for(unsigned i=0;i<AX_CELLS;i++){
            struct ax_cell *c=&b->ax.cells[i];
            if(!c->state){c->state=3;return 1;}
            if(b->vm->globals[0][50].number&0x10){if(c->state!=AX_STOPPED&&c->state!=4)return 1;}
            else if(c->state==3)c->state=4;
        }
        b->ax_modal=0;
    }
    return 0;
}
static int ax_script_command(KBootstrap *b,int sub){
    if(sub<1||sub>11)return 0;
    if(sub==6||sub==7||sub==10||sub==11){
        for(unsigned i=0;i<AX_CELLS;i++){
            struct ax_cell *c=&b->ax.cells[i];
            if(sub==6&&c->state!=AX_STOPPED)c->state=1;
            if(sub==7)c->state=AX_STOPPED;
            if(sub==10&&!c->state)c->state=3;
            if(sub==11&&c->state==4)c->state=0;
        }
        if(sub==7){b->ax.wait_cell=0;b->ax_modal=0;}
        if(sub==10)b->ax_modal=2;
    }else{
        int32_t bank,cell;if(integer(b,&bank)||integer(b,&cell))return -1;
        if(bank<0||bank>=10||cell<0||cell>=32||!b->ax.size)return error(b,"AX script track range");
        unsigned index=(unsigned)bank*32+(unsigned)cell;struct ax_cell *c=&b->ax.cells[index];
        if(sub==1){uint32_t start=c->start;memset(c,0,sizeof(*c));c->start=start;c->state=AX_STOPPED;}
        if(sub==2||sub==5){
            if(c->start<0x500||c->start>=b->ax.size||c->ip>=b->ax.size-c->start)return error(b,"AX script start invalid");
            c->state=0;
        }
        if(sub==3)c->state=1;
        if(sub==4)c->state=AX_STOPPED;
        if(sub==5){b->ax.wait_cell=index+1;b->ax_modal=1;}
        if(sub==8){if(!c->state)c->state=3;b->ax_modal=2;}
        if(sub==9&&c->state==4)c->state=0;
    }
    for(unsigned i=0;i<AX_CELLS;i++)b->animation_status[i]=(uint8_t)b->ax.cells[i].state;
    return 0;
}
static int ax_modal_skip(KBootstrap *b){
    if(b->ax_modal!=1)return 0;
    if(b->ax.wait_cell)b->ax.cells[b->ax.wait_cell-1].state=AX_STOPPED;
    b->ax.wait_cell=0;b->ax_modal=0;return 1;
}
