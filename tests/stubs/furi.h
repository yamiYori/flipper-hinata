#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
typedef enum { FuriStatusOk, FuriStatusErrorTimeout, FuriStatusErrorResource } FuriStatus;
typedef struct FuriMessageQueue FuriMessageQueue;
typedef struct FuriSemaphore FuriSemaphore;
#define UNUSED(x) (void)(x)
#define FURI_CRITICAL_ENTER()                                                                      \
    do {                                                                                           \
    } while(0)
#define FURI_CRITICAL_EXIT()                                                                       \
    do {                                                                                           \
    } while(0)
FuriMessageQueue *furi_message_queue_alloc(uint32_t n, uint32_t size);
void furi_message_queue_free(FuriMessageQueue *q);
FuriStatus furi_message_queue_put(FuriMessageQueue *q, const void *p, uint32_t t);
FuriStatus furi_message_queue_get(FuriMessageQueue *q, void *p, uint32_t t);
FuriSemaphore *furi_semaphore_alloc(uint32_t max, uint32_t initial);
void furi_semaphore_free(FuriSemaphore *s);
FuriStatus furi_semaphore_acquire(FuriSemaphore *s, uint32_t t);
FuriStatus furi_semaphore_release(FuriSemaphore *s);
static inline uint32_t furi_ms_to_ticks(uint32_t n) { return n; }
