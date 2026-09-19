#ifndef KISAKU_SCENE_H
#define KISAKU_SCENE_H
#include <stdint.h>
#include <stddef.h>
#define KSCENE_COUNT 370
typedef struct {uint32_t order,id;int32_t x,y,width,height;} KSceneNode;
typedef struct {
    KSceneNode nodes[KSCENE_COUNT];unsigned count;
    uint8_t visited[1000],status[KSCENE_COUNT],flags[KSCENE_COUNT];
    uint16_t counters[KSCENE_COUNT]; /* Native words[11..380]: chronological path, not counters. */
    unsigned path_count;
    uint32_t route_group[KSCENE_COUNT];
} KScene;
int kscene_map(KScene *s,const uint8_t *data,size_t size);
int kscene_restore(KScene *s,const uint8_t *bytes,size_t byte_count,const uint8_t *raw,size_t raw_count);
int kscene_complete(KScene *s,int scene,int part);
unsigned kscene_module(const char *name);
int kscene_edge(unsigned from,unsigned to);
int kscene_path_restore(KScene *s,const uint16_t *words,size_t count,const char *module);
int kscene_transition(KScene *s,const char *from,const char *to,unsigned flag291);
#endif
