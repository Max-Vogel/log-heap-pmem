#include "log_pmem_types.h"

int is_log_initialized(pmem_t *pmem);

int init_log(pmem_t *pmem);

pmo_t append_to_log(pmem_t *pmem, log_entry_t *log_entry);

int append_head_to_used_segments_and_get_new_head_segment(pmem_t *pmem);

uint64_t gen_id(pmem_t *pmem);
