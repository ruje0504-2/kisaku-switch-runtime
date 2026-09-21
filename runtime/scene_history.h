#ifndef KISAKU_SCENE_HISTORY_H
#define KISAKU_SCENE_HISTORY_H
#include <stdint.h>
#include <stddef.h>
#define KHISTORY_SIZE 22204
typedef struct {uint8_t data[KHISTORY_SIZE];unsigned loaded;} KSceneHistory;
typedef struct {unsigned scene,part,completion,checkpoint;char module[32];} KSceneCheckpoint;
int khistory_checkpoint(const KSceneHistory *h,unsigned slot,KSceneCheckpoint *out);
int khistory_catalog(unsigned slot,unsigned *scene,unsigned *part);
int khistory_load(KSceneHistory *h,const char *root);
int khistory_register(KSceneHistory *h,const char *root,int scene,int part,const char *module,int32_t checkpoint);
/* value=-1 queries previous without modifying data. */
int khistory_completion(KSceneHistory *h,const char *root,int scene,int part,int value,int *previous);
#endif
