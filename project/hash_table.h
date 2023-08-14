#include "log_pmem_types.h"

void init_hash_table(pmem_t *pmem);

void add_to_hash_table(pmem_t *pmem, uint64_t id, pmo_t offset);

pmo_t get_from_hash_table(pmem_t *pmem, uint64_t id);

void remove_from_hash_table(pmem_t *pmem, uint64_t id);

void destroy_hash_table(pmem_t *pmem);

void reconstruct_hash_table(pmem_t *pmem);

void persist_hash_table(pmem_t *pmem);
