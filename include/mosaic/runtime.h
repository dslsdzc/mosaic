#ifndef MOSAIC_RUNTIME_H
#define MOSAIC_RUNTIME_H
#include "mosaic/pack.h"

typedef struct mosaic_runtime mosaic_runtime;

mosaic_runtime *mosaic_runtime_open(const char *pack_path, char *errbuf, size_t errlen);
void mosaic_runtime_close(mosaic_runtime *rt);
u32 mosaic_runtime_last_error(const mosaic_runtime *rt);
u64 mosaic_runtime_function_count(const mosaic_runtime *rt);
const mosaic_module_record *mosaic_runtime_find_module(mosaic_runtime *rt, u64 module_id);
const mosaic_function_record *mosaic_runtime_find_function(mosaic_runtime *rt, u64 fn_id);
const char *mosaic_runtime_module_string(const mosaic_runtime *rt, const mosaic_module_record *m, u32 off);
u32 mosaic_runtime_event_id(mosaic_runtime *rt, const char *name);
#endif
