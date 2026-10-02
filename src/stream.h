#ifndef ___STREAM_H
#define ___STREAM_H

#include <stddef.h>
#include <stdint.h>

#define MAX_FRAME (256 * 1024)

int stream_init();
void stream_update(const void *data, size_t size);
int stream_get(uint8_t *buffer, size_t *size, uint32_t *version, uint32_t *frame_count);
extern uint64_t stream_frame_count();
void stream_destroy();
int stream_save_snapshot(const char *filename);


#endif