#ifndef __PATCH_UTIL_H__
#define __PATCH_UTIL_H__


#include "patch.h"


/* utility functions */
int         patch_count           (void);
int         patch_create          (const char* name);
int         patch_destroy         (int id);
void        patch_destroy_all     (void);
int         patch_dump            (int** dump);
int         patch_duplicate       (int id);
int         patch_flush           (int id);
void        patch_flush_all       (void);
void        patch_init            (void);
const char* patch_strerror        (int error);
int         patch_sample_load     (int id, const char* file);
void        patch_sample_unload   (int id);
void        patch_set_buffersize  (int nframes);
void        patch_set_samplerate  (int rate);
void        patch_shutdown        (void);
void        patch_sync            (float bpm);
int         patch_verify          (int id);


#endif /* __PATCH_UTIL_H__ */