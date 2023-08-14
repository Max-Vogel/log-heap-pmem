#include "log_pmem_types.h"

int init_pmem(pmem_t *pmem, char *path);

int delete_pmem(pmem_t *pmem);

uint64_t palloc(pmem_t *pmem, size_t size, void *data);

void pfree(pmem_t *pmem, uint64_t id);

void * get_address(pmem_t *pmem, uint64_t id);

int update_data(pmem_t *pmem, uint64_t id, void *data, size_t size);
