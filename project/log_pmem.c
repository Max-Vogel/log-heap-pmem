#include "cleaner.h"
#include "hash_table.h"
#include "log.h"
#include "log_pmem.h"


void err_config(pmem_t *pmem) {
    close(pmem->fd);
    free(pmem->path_to_pmem);
}

void err_granularity(pmem_t *pmem, struct pmem2_config *cfg) {
    pmem2_config_delete(&cfg);
    err_config(pmem);
}

void err_map(pmem_t *pmem, struct pmem2_config *cfg, struct pmem2_source *src) {
    pmem2_source_delete(&src);
    err_granularity(pmem, cfg);
}

void err_src_delete(pmem_t *pmem, struct pmem2_config *cfg, struct pmem2_source *src) {
    pmem2_map_delete(&pmem->map);
    err_map(pmem, cfg, src);
}

int init_pmem(pmem_t *pmem, char *path) {
    pmem->fd = open(path, O_RDWR);
    if(pmem->fd < 0) {
        perror("open");
        return 1;
    }

    pmem->path_to_pmem = malloc(strlen(path) + 1);
    if(!pmem->path_to_pmem) {
        perror("malloc");
        return 1;
    }
    strcpy(pmem->path_to_pmem, path);

    struct pmem2_config *cfg;
    if(pmem2_config_new(&cfg)) {
        pmem2_perror("pmem2_config_new");
        err_config(pmem);
        return 1;
	}

    if(pmem2_config_set_required_store_granularity(cfg, PMEM2_GRANULARITY_PAGE)) {
        pmem2_perror("pmem2_config_set_required_store_granularity");
        err_granularity(pmem, cfg);
        return 1;
	}

	struct pmem2_source *src;
    if(pmem2_source_from_fd(&src, pmem->fd)) {
        pmem2_perror("pmem2_source_from_fd");
        err_granularity(pmem, cfg);
        return 1;
	}

    // size_t alignment;
    // if(pmem2_source_alignment(src, &alignment)) {
    //     pmem2_perror("pmem2_source_alignment");
    //     return 1;
    // }

    // if(pmem2_config_set_length(cfg, LOG_LENGTH)) {
    //     pmem2_perror("pmem2_config_set_length");
    // }

    if(pmem2_map_new(&pmem->map, cfg, src)) {
        pmem2_perror("pmem2_map_new");
        err_map(pmem, cfg, src);
        return 1;
	}
    
    pmem->size = pmem2_map_get_size(pmem->map) - sizeof(log_t);
    if(pmem->size < MIN_LOG_LENGTH) {
        fprintf(stderr, "init_pmem: file size to small\n");
        err_src_delete(pmem, cfg, src);
        return 1;
    }
    pmem->persist = pmem2_get_persist_fn(pmem->map);
    pmem->memcpy_fn = pmem2_get_memcpy_fn(pmem->map);
    pmem->log = pmem2_map_get_address(pmem->map);

    pthread_mutex_init(&pmem->update_move_mutex, NULL);
    pthread_mutex_init(&pmem->free_segs_mutex, NULL);
    pthread_mutex_init(&pmem->used_segs_mutex, NULL);
    pthread_mutex_init(&pmem->cleaner_segs_mutex, NULL);
    pthread_mutex_init(&pmem->ermergency_segs_mutex, NULL);
    pthread_mutex_init(&pmem->curr_cleaned_mutex, NULL);
    
    if(!is_log_initialized(pmem)) {
        init_log(pmem);
    }

    init_hash_table(pmem);
    if(!pmem->log->regular_termination) {
        append_all_cleaner_segments_to_free_segments(pmem);
        reconstruct_hash_table(pmem);
    } else {
        if(get_persisted_hash_table(pmem)) {
            reconstruct_hash_table(pmem);
        }
    }
    init_cleaner(pmem);

    pmem->log->regular_termination = 0;
    pmem->persist(&pmem->log->regular_termination, sizeof(uint8_t));

	if(pmem2_source_delete(&src)) {
        pmem2_perror("pmem2_source_delete");
        err_src_delete(pmem, cfg, src);
        return 1;
	}

	if(pmem2_config_delete(&cfg)) {
        pmem2_perror("pmem2_config_delete");
        err_src_delete(pmem, cfg, src);
        return 1;
	}

    return 0;
}

int delete_pmem(pmem_t *pmem) {
    int ret = 0;
    
    terminate_cleaner(pmem);
    persist_hash_table(pmem);
    destroy_hash_table(pmem);

    pmem->log->regular_termination = 1;
    pmem->persist(&pmem->log->regular_termination, sizeof(uint8_t));

    pthread_mutex_destroy(&pmem->update_move_mutex);
    pthread_mutex_destroy(&pmem->free_segs_mutex);
    pthread_mutex_destroy(&pmem->used_segs_mutex);
    pthread_mutex_destroy(&pmem->cleaner_segs_mutex);
    pthread_mutex_destroy(&pmem->ermergency_segs_mutex);
    pthread_mutex_destroy(&pmem->curr_cleaned_mutex);

    if(pmem2_map_delete(&pmem->map)) {
        pmem2_perror("pmem2_map_delete");
        ret = 1;
	}

	if(close(pmem->fd)) {
        perror("close");
        ret = 1;
	}

    free(pmem->path_to_pmem);

    return ret;
}

uint64_t palloc(pmem_t *pmem, size_t size, void *data) {
    // printf("palloc\n");
    if(size == 0) {
        return 0;
    }

    log_entry_t *log_entry = malloc(sizeof(log_entry_t) + sizeof(uint8_t) * size);
    if(!log_entry) {
        perror("malloc");
        return 0;
    }

    uint64_t id = gen_id(pmem);
    log_entry->id = id;
    log_entry->version = 1;
    log_entry->length = size;
    log_entry->to_delete = 0;
    memcpy(log_entry->data, data, size);

    pmo_t pmem_offset = append_to_log(pmem, log_entry);
    if(!pmem_offset) {
        return 0;
    }
    add_to_hash_table(pmem, log_entry->id, pmem_offset);
    free(log_entry);

    return id;
}

uint64_t palloc_with_id(pmem_t *pmem, uint64_t id, size_t size, void *data) {
    // printf("palloc_with_id\n");
    if(size == 0) {
        return 1;
    }

    if(hash_table_contains(pmem, id)) {
        return update_data(pmem, id, data, size);
    }

    log_entry_t *log_entry = malloc(sizeof(log_entry_t) + sizeof(uint8_t) * size);
    if(!log_entry) {
        perror("malloc");
        return 1;
    }

    log_entry->id = id;
    log_entry->version = 1;
    log_entry->length = size;
    log_entry->to_delete = 0;
    memcpy(log_entry->data, data, size);

    pmo_t pmem_offset = append_to_log(pmem, log_entry);
    if(!pmem_offset) {
        return 1;
    }
    add_to_hash_table(pmem, log_entry->id, pmem_offset);
    free(log_entry);

    return 0;
}

void pfree(pmem_t *pmem, uint64_t id) {
    // printf("pfree\n");
    pthread_mutex_lock(&pmem->update_move_mutex);
    pmo_t off = get_from_hash_table(pmem, id);
    log_entry_t *log_entry = offset_to_pointer(pmem, off);
    if(!log_entry) {
        return;
    }

    // log_entry zum löschen markieren
    log_entry->to_delete = 1;
    pmem->persist(&log_entry->to_delete, sizeof(uint8_t));
    
    // used_space im Segment verringern
    uint64_t segment_id = (off - sizeof(log_t)) / SEGMENT_SIZE;
    segment_t *segment = (segment_t*)((uint8_t*)pmem->log + sizeof(log_t) + segment_id * SEGMENT_SIZE); 
    segment->used_space -= log_entry->length + sizeof(log_entry_t);
    pmem->persist(&segment->used_space, sizeof(size_t));

    remove_from_hash_table(pmem, id);
    pthread_mutex_unlock(&pmem->update_move_mutex);
}

void * get_address(pmem_t *pmem, uint64_t id) {
    pmo_t off = get_from_hash_table(pmem, id);
    log_entry_t *log_entry = offset_to_pointer(pmem, off);
    if(!log_entry) {
        return NULL;
    }
    return log_entry->data;
}

int update_data(pmem_t *pmem, uint64_t id, void *data, size_t size) {
    // printf("update_data\n");
    if(size == 0) {
        return 1;
    }

    log_entry_t *updated_log_entry = malloc(sizeof(log_entry_t) + sizeof(uint8_t) * size);
    if(!updated_log_entry) {
        perror("malloc");
        return 1;
    }   

    pthread_mutex_lock(&pmem->update_move_mutex);
    pmo_t off = get_from_hash_table(pmem, id);
    log_entry_t *log_entry = offset_to_pointer(pmem, off);
    if(!log_entry) {
        fprintf(stderr, "id does not exist in pmem");
        return 1;
    }

    memcpy(updated_log_entry, log_entry, sizeof(log_entry_t));
    updated_log_entry->length = size;
    updated_log_entry->version++;
    memcpy(updated_log_entry->data, data, size);

    pmo_t offset = append_to_log(pmem, updated_log_entry);
    if(!offset) {
        pthread_mutex_unlock(&pmem->update_move_mutex);
        return 1;
    }
    add_to_hash_table(pmem, id, offset);
    pthread_mutex_unlock(&pmem->update_move_mutex);

    return 0;
}
