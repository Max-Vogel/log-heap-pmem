#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <libpmem2.h>
#include "log.h"

#define SEGMENT_SIZE (1U << 12)  // 4 KiB
#define LOG_LENGTH (1U << 23) // 8 MiB
#define LOG_SIGNATURE "log is initialized"
#define LOG_SIGNATURE_LEN sizeof(LOG_SIGNATURE)


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
    pmo_t free_segments;
    pmo_t used_segments;
    pmo_t head_segment;
    pmo_t last_used_segment;
    pmo_t cleaner_head;
    // segment_t segments[];
} log_t;

typedef struct pmem {
    log_t *log;
    int fd;
    struct pmem2_map *map;
	pmem2_persist_fn persist;
    pmem2_memcpy_fn memcpy_fn;
    size_t size;
} pmem_t;


void * offset_to_pointer(pmem_t *pmem, pmo_t offset) {
    return offset == 0 ? NULL : (uint8_t*)pmem->log + offset;
}

pmo_t pointer_to_offset(pmem_t *pmem, void *p) {
    return p == NULL ? 0 : (pmo_t)((uint8_t*)p - (uint8_t*)pmem->log); 
}

int is_log_initialized(pmem_t *pmem) {
    return memcmp(pmem->log->signature, LOG_SIGNATURE, LOG_SIGNATURE_LEN) == 0;
}

int init_log(pmem_t *pmem) {
    log_t *log = pmem->log;
    segment_t *prev_seg = NULL;
    segment_t *curr_seg = (segment_t*)((uint8_t*)log + sizeof(log_t));

    log->free_segments = pointer_to_offset(pmem, curr_seg);
    log->used_segments = pointer_to_offset(pmem, NULL);
    log->head_segment = pointer_to_offset(pmem, NULL);
    log->last_used_segment = pointer_to_offset(pmem, NULL);
    log->cleaner_head = pointer_to_offset(pmem, NULL);
    pmem->persist(log, sizeof(log_t));

    // Segmente initialisieren
    for(int i=0, seg_id=0; i<pmem->size-SEGMENT_SIZE; i+=SEGMENT_SIZE, seg_id++) {
        curr_seg->id = seg_id;
        curr_seg->used_space = 0;
        prev_seg = curr_seg;
        curr_seg = (segment_t*)((uint8_t*)curr_seg + sizeof(uint8_t) * SEGMENT_SIZE);
        prev_seg->next_segment = pointer_to_offset(pmem, curr_seg);
        pmem->persist(prev_seg, sizeof(segment_t));
    }
    prev_seg->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&prev_seg->next_segment, sizeof(pmo_t));

    // Head Segment holen
    log->head_segment = log->free_segments;
    log->free_segments = ((segment_t*)offset_to_pointer(pmem, log->free_segments))->next_segment;
    pmem->persist(log, sizeof(log_t));

    segment_t *head_segment = ((segment_t*)offset_to_pointer(pmem, log->head_segment));
    head_segment->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&head_segment->next_segment, sizeof(pmo_t));


    pmem->memcpy_fn(log->signature, LOG_SIGNATURE, LOG_SIGNATURE_LEN, 0);

    return 0;
}

int init_pmem(pmem_t *pmem, char *path) { // TODO: In Fehlerabfragen Speicher freigeben
    pmem->fd = open(path, O_RDWR);
    if(pmem->fd < 0) {
        perror("open");
        return 1;
    }

    struct pmem2_config *cfg;
    if(pmem2_config_new(&cfg)) {
        pmem2_perror("pmem2_config_new");
        return 1;
	}

    if(pmem2_config_set_required_store_granularity(cfg, PMEM2_GRANULARITY_PAGE)) {
        pmem2_perror("pmem2_config_set_required_store_granularity");
        return 1;
	}

	struct pmem2_source *src;
    if(pmem2_source_from_fd(&src, pmem->fd)) {
        pmem2_perror("pmem2_source_from_fd");
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
        return 1;
	}
    
    // TODO: Größe prüfen
    pmem->size = pmem2_map_get_size(pmem->map) - sizeof(log_t);
    pmem->persist = pmem2_get_persist_fn(pmem->map);
    pmem->memcpy_fn = pmem2_get_memcpy_fn(pmem->map);
    pmem->log = pmem2_map_get_address(pmem->map);

    if(!is_log_initialized(pmem)) {
        init_log(pmem);
        // printf("hallo\n");
    }

	if(pmem2_source_delete(&src)) {
        pmem2_perror("pmem2_source_delete");
        return 1;
	}

	if(pmem2_config_delete(&cfg)) {
        pmem2_perror("pmem2_config_delete");
        return 1;
	}

    return 0;
}

int delete_pmem(pmem_t *pmem) {
    if(pmem2_map_delete(&pmem->map)) {
        pmem2_perror("pmem2_map_delete");
        return 1;
	}

	if(close(pmem->fd)) {
        perror("close");
        return 1;
	}

    return 0;
}

uint64_t palloc(pmem_t *pmem, size_t size, void *data) {
    log_entry_t *log_entry = malloc(sizeof(log_entry_t) + sizeof(uint8_t) * size);
    if(!log_entry) {
        perror("malloc");
        return 0;
    }

    uint64_t id = gen_id();
    log_entry->id = id;
    log_entry->version = 1;
    log_entry->length = size;
    log_entry->to_delete = 0;
    memcpy(log_entry->data, data, size);

    pmo_t pmem_offset = append_to_log(pmem, log_entry);
    add_to_hash_table(log_entry->id, pmem_offset);
    free(log_entry);

    return id;
}

void pfree(pmem_t *pmem, uint64_t id) {
    pmo_t off = get_from_hash_table(id);
    log_entry_t *log_entry = offset_to_pointer(pmem, off);
    log_entry->to_delete = 1;
    pmem->persist(&log_entry->to_delete, sizeof(uint8_t));
    
    uint64_t segment_id = (off - sizeof(log_t)) / SEGMENT_SIZE;
    segment_t *segment = (uint8_t*)pmem->log + segment_id * SEGMENT_SIZE; 
    segment->used_space -= log_entry->length + sizeof(log_entry_t);
    pmem->persist(&segment->used_space, sizeof(size_t));

    remove_from_hash_table(id);
}

void * get_address(pmem_t *pmem, uint64_t id) {
    pmo_t off = get_from_hash_table(id);
    log_entry_t *log_entry = offset_to_pointer(pmem, off);
    return log_entry->data;
}

int update_data(pmem_t *pmem, uint64_t id, void *data, size_t size) {
    pmo_t off = get_from_hash_table(id);
    log_entry_t *log_entry = offset_to_pointer(pmem, off);

    log_entry_t *updated_log_entry = malloc(sizeof(log_entry_t) + sizeof(uint8_t) * size);
    if(!log_entry) {
        perror("malloc");
        return 1;
    }   

    memcpy(updated_log_entry, log_entry, sizeof(log_entry_t));
    updated_log_entry->length = size;
    updated_log_entry->version++;
    memcpy(updated_log_entry->data, data, size);

    pmo_t offset = append_to_log(pmem, updated_log_entry);
    add_to_hash_table(id, offset);

    return 0;
}

pmo_t append_to_log(pmem_t *pmem, log_entry_t *log_entry) {
    log_t *log = pmem->log;
    size_t log_entry_size = sizeof(log_entry_t) + sizeof(uint8_t) * log_entry->length;
    segment_t* head_seg = offset_to_pointer(pmem, log->head_segment);

    if(SEGMENT_SIZE - head_seg->used_space < log_entry_size) {
        append_head_to_used_segments_and_get_new_head_segment(pmem);
        head_seg = offset_to_pointer(pmem, log->head_segment);
    }

    uint8_t *log_entry_start = (uint8_t*)head_seg->log_entries + head_seg->used_space; 
    pmem->memcpy_fn(log_entry_start, log_entry, log_entry_size, 0);
    pmo_t pmem_offset = pointer_to_offset(pmem, log_entry_start);
    head_seg->used_space += log_entry_size;
    pmem->persist(&head_seg->used_space, sizeof(size_t));
    
    return pmem_offset;
}

void append_head_to_used_segments_and_get_new_head_segment(pmem_t *pmem) {
    log_t *log = pmem->log;

    // Head Segment an benutzte hängen
    segment_t *last_used_segment = (segment_t*)offset_to_pointer(pmem, log->last_used_segment);
    if(!last_used_segment) {
        log->used_segments = log->head_segment;
    } else {
        last_used_segment->next_segment = log->head_segment;
        pmem->persist(&last_used_segment->next_segment, sizeof(pmo_t));
    }
    log->last_used_segment = log->head_segment;

    // neues Head Segment holen
    log->head_segment = log->free_segments;
    log->free_segments = ((segment_t*)offset_to_pointer(pmem, log->free_segments))->next_segment;
    pmem->persist(log, sizeof(log_t));

    segment_t *head_segment = ((segment_t*)offset_to_pointer(pmem, log->head_segment));
    head_segment->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&head_segment->next_segment, sizeof(pmo_t));
}

// TODO: implementieren
void add_to_hash_table(uint64_t id, pmo_t offset) {

}

// TODO: implementieren
pmo_t get_from_hash_table(uint64_t id) {
    return 0;
}

// TODO: implementieren
void remove_from_hash_table(uint64_t id) {

}

// TODO: implementieren
uint64_t gen_id() {
    return 0;
}

int clean(pmem_t *pmem) {
    log_t *log = pmem->log;
    segment_t *curr_seg = offset_to_pointer(pmem, log->used_segments);
    segment_t *cleaned_seg = offset_to_pointer(pmem, log->free_segments);
    
}



void dump_seg_ids(pmem_t *pmem) {
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->free_segments);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        printf("%ld\n", curr_seg->id);
    }
}

int main(void) {
    pmem_t pmem;
    char *path = "test";
    if(init_pmem(&pmem, path)) {
        return 1;
    }

    // dump_seg_ids(&pmem);

    if(delete_pmem(&pmem)) {
        return 1;
    }

    return 0;
}