#include "param_change.h"
#include "ax.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static unsigned emit(uint8_t *p,unsigned op,uint32_t arg){p[0]=(uint8_t)op;put32(p+1,arg);return 5;}
int main(void){
    KParamChange p={0},before;
    const int16_t initial[4]={800,2,990,78};
    const int32_t codes[4]={1005,990,1100,1005};
    assert(!kparam_change_plan(&p,initial,20,100,codes));
    assert(p.target[0]==805&&p.target[1]==0&&p.target[2]==999&&p.target[3]==80);
    assert(p.total_target==22&&p.steps==100&&p.increment[0]==0.05f);
    const int32_t same[4]={1000,1000,1000,1000};
    assert(!kparam_change_plan(&p,initial,20,0,same));
    for(unsigned i=0;i<4;i++)assert(p.target[i]==initial[i]&&p.increment[i]==0);
    const int32_t wrapped[4]={0x103ed,-1,0,0};
    assert(!kparam_change_plan(&p,initial,20,0x10001,wrapped));
    assert(p.steps==1&&p.target[0]==805&&p.target[1]==999&&p.target[2]==0&&p.target[3]==0);
    assert(p.total_target==(uint16_t)(20-78));
    before=p;const int16_t invalid[4]={1000,2,3,4};
    assert(kparam_change_plan(&p,invalid,0,100,codes)<0&&!memcmp(&p,&before,sizeof(p)));
    uint8_t data[0x600]={0};put32(data,0x580);unsigned at=0x580;
    at+=emit(data+at,5,2); /* outer loop twice */
    at+=emit(data+at,7,3); /* inner loop three times */
    at+=emit(data+at,1,20);at+=emit(data+at,2,4);
    at+=emit(data+at,9,0); /* descriptor, not a frame boundary */
    data[at++]=8;data[at++]=6;data[at++]=4;
    struct ax_player a,saved;assert(ax_load(&a,"synthetic.ax",data,at));saved=a;
    int32_t count=123;assert(ax_count_boundaries(&a,0,&count)&&count==6);
    assert(!memcmp(&a,&saved,sizeof(a)));
    a.data[at-1]=3;assert(ax_count_boundaries(&a,0,&count)&&count==-1);
    a.data[at-1]=255;assert(ax_count_boundaries(&a,0,&count)&&count==6);
    a.size=0x581;count=123;assert(!ax_count_boundaries(&a,0,&count)&&count==123);
    a=saved;a.data[0x580]=6;assert(!ax_count_boundaries(&a,0,&count)&&count==123);
    a=saved;put32(a.data+0x581,0); /* endless outer loop: explicit failure */
    assert(!ax_count_boundaries(&a,0,&count)&&count==123);
    /* Native boundary duration includes the opcode tick. A delay opcode
       instead consumes its full count after the opcode tick. */
    memset(data,0,sizeof(data));put32(data,0x580);at=0x580;
    at+=emit(data+at,1,1);at+=emit(data+at,2,2);data[at++]=255;
    assert(ax_load(&a,"native-ticks.ax",data,at));assert(ax_control(&a,1,0,0));
    uint8_t events[AX_CELLS]={0};
    assert(ax_tick_native(&a,events,NULL,NULL)&&events[0]==2&&a.cells[0].boundary_delay==0);
    assert(ax_tick_native(&a,events,NULL,NULL)&&events[0]==3&&a.cells[0].delay==2);
    assert(ax_tick_native(&a,events,NULL,NULL)&&events[0]==3&&a.cells[0].delay==1);
    assert(ax_tick_native(&a,events,NULL,NULL)&&events[0]==3&&a.cells[0].delay==0);
    assert(ax_tick_native(&a,events,NULL,NULL)&&events[0]==10&&a.cells[0].state==AX_STOPPED);
    /* A pending stop at a boundary still emits the boundary event. */
    assert(ax_load(&a,"stop.ax",data,at));assert(ax_control(&a,2,0,0));
    assert(ax_tick_native(&a,events,NULL,NULL)&&events[0]==2&&a.cells[0].state==AX_STOPPED);
    puts("Kisaku native AX event IDs, boundary duration and pending stop: PASS");
    puts("Kisaku parameter targets, unsigned operands, limits and native AX boundary counting: PASS");
}
