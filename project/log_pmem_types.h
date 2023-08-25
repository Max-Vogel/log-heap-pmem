#ifndef LOG_PMEM_TYPES_H
#define LOG_PMEM_TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <libpmem2.h>
#include <glib.h>

#define SEGMENT_SIZE (1U << 12)  // 4 KiB
#define LOG_LENGTH (1U << 23) // 8 MiB
#define LOG_SIGNATURE "log is initialized"
#define LOG_SIGNATURE_LEN sizeof(LOG_SIGNATURE)
#define CLEANER_THREAD_COUNT 1
#define EMERGENCY_CLEANER_SEGMENT_COUNT 1

// persistent memory offset
typedef uintptr_t pmo_t;

typedef struct log_entry {
    uint64_t id;
    uint64_t version;
    size_t length;
    uint8_t to_delete;
    uint8_t data[];
} log_entry_t;

typedef struct segment {
    uint64_t id;
    pmo_t next_segment;
    size_t used_space;
    log_entry_t log_entries[];
} segment_t;

typedef struct log {
    uint8_t signature[LOG_SIGNATURE_LEN];
    uint8_t regular_termination;
    pmo_t head_segment;
    pmo_t free_segments;
    pmo_t last_free_segment;
    pmo_t used_segments;
    pmo_t last_used_segment;
    pmo_t cleaner_segments;
    pmo_t last_cleaner_segment;
    pmo_t emergency_cleaner_segments;
    pmo_t last_emergency_cleaner_segment;
    uint64_t current_id;
    size_t head_position;
    size_t used_segments_count;
    size_t current_emergency_cleaner_segments_count;
} log_t;

typedef struct pmem {
    log_t *log;
    int fd;
    struct pmem2_map *map;
    pmem2_persist_fn persist;
    pmem2_memcpy_fn memcpy_fn;
    size_t size;
    pthread_t cleaner_thread_refs[CLEANER_THREAD_COUNT];
    uint8_t terminate_cleaner_threads;
    GHashTable *hash_table;
    char *path_to_pmem;

    pthread_mutex_t head_seg_mutex;
    pthread_mutex_t free_segs_mutex;
    pthread_mutex_t used_segs_mutex;
    pthread_mutex_t cleaner_segs_mutex;
    pthread_mutex_t ermergency_segs_mutex;
} pmem_t;

void * offset_to_pointer(pmem_t *pmem, pmo_t offset);

pmo_t pointer_to_offset(pmem_t *pmem, void *p);

#endif
