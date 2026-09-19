#ifndef KISAKU_READ_FLAGS_H
#define KISAKU_READ_FLAGS_H
#include <stddef.h>
#include <stdint.h>
int kread_flags_load(const char *root,unsigned selector,uint8_t *data,size_t size);
int kread_flags_save(const char *root,unsigned selector,const uint8_t *data,size_t size);
#endif
