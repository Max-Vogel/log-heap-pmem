#include "log_pmem_types.h"

typedef struct key_value_pair {
    uint64_t key;
    pmo_t value;
} kvp_t;

typedef struct hash_table_meta {
    uint8_t succesfully_persisted;
    size_t counter;
    kvp_t data[];
} hash_meta_t;

typedef struct hash_table_pmem {
    hash_meta_t *hash_meta;
    int fd;
    struct pmem2_map *map;
    pmem2_persist_fn persist;
    pmem2_memcpy_fn memcpy_fn;
    size_t size;
} hash_pmem_t;

void init_hash_table(pmem_t *pmem);

void add_to_hash_table(pmem_t *pmem, uint64_t id, pmo_t offset);

pmo_t get_from_hash_table(pmem_t *pmem, uint64_t id);

void remove_from_hash_table(pmem_t *pmem, uint64_t id);

void destroy_hash_table(pmem_t *pmem);

int hash_table_contains(pmem_t *pmem, uint64_t id);

void reconstruct_hash_table(pmem_t *pmem);

int get_persisted_hash_table(pmem_t *pmem);

int persist_hash_table(pmem_t *pmem);

void persist_key_value_pair(gpointer key, gpointer value, gpointer user_data);

int init_hash_pmem(pmem_t *pmem, hash_pmem_t *hpmem);

int delete_hash_pmem(hash_pmem_t *hpmem);

void err_config_hash(hash_pmem_t *hpmem);

void err_granularity_hash(hash_pmem_t *hpmem, struct pmem2_config *cfg);

void err_map_hash(hash_pmem_t *hpmem, struct pmem2_config *cfg, struct pmem2_source *src);

void err_src_delete_hash(hash_pmem_t *hpmem, struct pmem2_config *cfg, struct pmem2_source *src);
