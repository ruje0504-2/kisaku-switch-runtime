#include "bowling.h"
#include <assert.h>
#include <stdio.h>
static unsigned events[128],count;
static KBowling *current;
static void destroy(void *p){assert(current);unsigned id=*(unsigned *)p;events[count++]=id;free(p);}
static void unregister_object(void *context,void *p){
    assert(context==&current&&current);events[count++]=1000+(p?*(unsigned *)p:0);
}
static KBowlingResource resource(unsigned id){unsigned *p=malloc(sizeof(*p));assert(p);*p=id;return (KBowlingResource){p,destroy};}
int main(void){
    KBowling b={0};current=&b;b.finish_state=9;
    assert(!kbowling_release(&b,&current)&&!current&&!b.finish_state);
    assert(!kbowling_release(&b,&current));
    current=&b;b.finish_state=8;unsigned borrowed[6]={11,12,13,14,15,16};
    for(unsigned i=0;i<45;i++)b.slots[i]=i>=17&&i<=22?(KBowlingResource){&borrowed[i-17],NULL}:resource(i+1);
    KBowlingObject **tail=&b.objects;
    for(unsigned i=0;i<3;i++){
        *tail=calloc(1,sizeof(**tail));assert(*tail);
        (*tail)->resource=i==2?(KBowlingResource){0}:resource(101+i);
        (*tail)->unregister=unregister_object;(*tail)->context=&current;tail=&(*tail)->next;
    }
    b.slots[44].destroy=NULL;
    assert(kbowling_release(&b,&current)<0&&count==0&&current==&b&&b.finish_state==8);
    b.slots[44].destroy=destroy;
    assert(!kbowling_release(&b,&current)&&!current&&!b.finish_state&&!b.objects);
    assert(count==44);unsigned at=0;
    for(unsigned i=0;i<45;i++){
        if(i>=17&&i<=22){assert(b.slots[i].object==&borrowed[i-17]&&borrowed[i-17]==i-6);continue;}
        unsigned expected=i==36?38:i==37?37:i+1;
        assert(events[at++]==expected&&!b.slots[i].object&&!b.slots[i].destroy);
    }
    assert(events[39]==1101&&events[40]==101&&events[41]==1102&&events[42]==102&&events[43]==1000);
    assert(!kbowling_release(&b,&current)&&count==44);
    puts("CBowling release: owned/borrowed resources, unregister order, invalid ownership and repeat calls: PASS");return 0;
}
