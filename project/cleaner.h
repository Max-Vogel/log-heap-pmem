#include "log_pmem_types.h"

int init_cleaner(pmem_t *pmem);

void * clean(void *param);

int to_clean(segment_t *segment);

segment_t * get_new_cleaner_segment(pmem_t *pmem);

void append_cleaner_segment_to_used_segments(pmem_t *pmem, uint64_t id);

void append_cleaned_segment_to_free_segments(pmem_t *pmem, uint64_t id);
