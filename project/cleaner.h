#include "log_pmem_types.h"

typedef struct cleaner_info {
    pmem_t *pmem;
    pmo_t segment;
    uint8_t regular_termination;
} cleaner_info_t;

int init_cleaner(pmem_t *pmem, uint8_t regular_termination);

int terminate_cleaner(pmem_t *pmem);

void * clean(void *param);

int wait_to_start_cleaning(pmem_t *pmem);

int to_clean(segment_t *segment);

int test_and_set_curr_cleaned(pmem_t *pmem, segment_t *segment);

void unset_curr_cleaned(pmem_t *pmem, segment_t *segment);

segment_t * get_new_cleaner_segment(pmem_t *pmem);

segment_t * get_emergency_cleaner_segment(pmem_t *pmem);

void refill_emergency_cleaner_segment(pmem_t *pmem, segment_t *curr_seg);

void append_cleaner_segment_to_used_segments(pmem_t *pmem, uint64_t id);

void append_cleaned_segment_to_free_segments(pmem_t *pmem, uint64_t id);

void append_cleaner_segment_to_free_segments(pmem_t *pmem, uint64_t id);

void append_all_cleaner_segments_to_used_segments(pmem_t *pmem);

void append_all_cleaner_segments_to_free_segments(pmem_t *pmem);
