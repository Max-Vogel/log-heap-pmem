#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <libpmem2.h>

typedef uintptr_t pmo_t;

typedef struct log_entry log_entry_t;

typedef struct segment segment_t;

typedef struct log log_t;

typedef struct pmem pmem_t;

void * offset_to_pointer(pmem_t *pmem, pmo_t offset);

pmo_t pointer_to_offset(pmem_t *pmem, void *p);

int is_log_initialized(pmem_t *pmem);

int init_log(pmem_t *pmem);

int init_pmem(pmem_t *pmem, char *path);

int delete_pmem(pmem_t *pmem);

uint64_t palloc(pmem_t *pmem, size_t size, void *data);

void pfree(pmem_t *pmem, uint64_t id);

void * get_address(pmem_t *pmem, uint64_t id);

int update_data(pmem_t *pmem, uint64_t id, void *data, size_t size);

pmo_t append_to_log(pmem_t *pmem, log_entry_t *log_entry);

void append_head_to_used_segments_and_get_new_head_segment(pmem_t *pmem);

void add_to_hash_table(uint64_t id, pmo_t offset);

pmo_t get_from_hash_table(uint64_t id);

void remove_from_hash_table(uint64_t id);

uint64_t gen_id();

int init_cleaner(pmem_t *pmem);

void * clean(void *param);

int to_clean(segment_t *segment);

segment_t * get_new_cleaner_segment(pmem_t *pmem);

void append_cleaner_segment_to_used_segments(pmem_t *pmem, uint64_t id);

void append_cleaned_segment_to_free_segments(pmem_t *pmem, uint64_t id);