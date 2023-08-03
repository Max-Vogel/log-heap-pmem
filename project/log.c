#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <libpmem2.h>
#include "log.h"

#define SEGMENT_SIZE (1U << 12)  // 4 KiB
#define LOG_LENGTH (1U << 23) // 8 MiB
#define LOG_SIGNATURE "log is initialized"
#define LOG_SIGNATURE_LEN sizeof(LOG_SIGNATURE)
#define CLEANER_THREAD_COUNT 1


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
    pmo_t head_segment;
    pmo_t free_segments;
    pmo_t last_free_segment;
    pmo_t used_segments;
    pmo_t last_used_segment;
    pmo_t cleaner_segments;
    pmo_t last_cleaner_segment;
    // segment_t segments[];
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

    // Segmentpointer bzw. Offsets initialisieren
    log->free_segments = pointer_to_offset(pmem, curr_seg);
    log->used_segments = pointer_to_offset(pmem, NULL);
    log->head_segment = pointer_to_offset(pmem, NULL);
    log->last_used_segment = pointer_to_offset(pmem, NULL);
    log->cleaner_segments = pointer_to_offset(pmem, NULL);
    log->last_cleaner_segment = pointer_to_offset(pmem, NULL);
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

    // Nachfolger von head_segment auf NULL setzen
    segment_t *head_segment = ((segment_t*)offset_to_pointer(pmem, log->head_segment));
    head_segment->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&head_segment->next_segment, sizeof(pmo_t));

    // Signatur setzen
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
    pmem->terminate_cleaner_threads = 1;

    for(int i=0; i<CLEANER_THREAD_COUNT; i++) {
        pthread_join(pmem->cleaner_thread_refs[i], NULL);
    }

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
    if(size == 0) {
        return 0;
    }

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

// FIXME: wenn Daten gelöscht werden, die im Headsegment sind, werden andere Daten bei der nächsten Allokation überschrieben
void pfree(pmem_t *pmem, uint64_t id) {
    pmo_t off = get_from_hash_table(id);
    log_entry_t *log_entry = offset_to_pointer(pmem, off);

    // log_entry zum löschen markieren
    log_entry->to_delete = 1;
    pmem->persist(&log_entry->to_delete, sizeof(uint8_t));
    
    // used_space im Segment verringern
    uint64_t segment_id = (off - sizeof(log_t)) / SEGMENT_SIZE;
    segment_t *segment = (segment_t*)((uint8_t*)pmem->log + segment_id * SEGMENT_SIZE); 
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
    if(size == 0) {
        return 1;
    }

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

    // wenn im head_segment nicht mehr genug Platz für den neuen Eintrag ist
    if(SEGMENT_SIZE - head_seg->used_space < log_entry_size) {
        // wenn das Ende des Segments leer ist und noch kleinere Daten als log_entry hineinpassen würden
        if(SEGMENT_SIZE - head_seg->used_space >= sizeof(log_entry_t)) {
            log_entry_t *unused_space = (log_entry_t*)((uint8_t*)head_seg->log_entries + head_seg->used_space);
            unused_space->length = 0;
            pmem->persist(&unused_space->length, sizeof(size_t));
        }
        append_head_to_used_segments_and_get_new_head_segment(pmem);
        head_seg = offset_to_pointer(pmem, log->head_segment);
    }

    // log_entry ins Log schreiben und used_space im Segment erhöhen
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

    // Nachfolger von head_segment auf NULL setzen
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

int init_cleaner(pmem_t *pmem) {
    pmem->terminate_cleaner_threads = 0;
    for(int i=0; i<CLEANER_THREAD_COUNT; i++) {
        if(pthread_create(&pmem->cleaner_thread_refs[i], NULL, &clean, pmem)) {
            perror("pthread_create");
            return 1;
        }
    }

    return 0;
}

void * clean(void *param) {
    pmem_t *pmem = (pmem_t*)param;
    log_t *log = pmem->log;
    segment_t *cleaner_seg = get_new_cleaner_segment(pmem);
    
    while(1) {
        segment_t *curr_seg = offset_to_pointer(pmem, log->used_segments);
        // alle Segmente durchgehen
        while(curr_seg) {
            // wenn der Cleaner gestoppt wird, werden die Cleanersegmente an used_segments angehangen
            if(pmem->terminate_cleaner_threads == 1) {
                append_cleaner_segment_to_used_segments(pmem, cleaner_seg->id);
                return NULL;
            }
            
            if(to_clean(curr_seg)) {
                int pos = 0;
                // alle log_entries durchgehen
                while(pos < SEGMENT_SIZE - sizeof(log_entry_t)) {
                    log_entry_t *log_entry = (log_entry_t*)((uint8_t*)curr_seg + sizeof(uint8_t) * pos);
                    size_t log_entry_size = sizeof(log_entry_t) + sizeof(uint8_t) * log_entry->length;

                    // wenn das Ende des Segments leer ist, das nächste Segment prüfen
                    if(log_entry->length == 0) {
                        break;
                    }

                    // wenn log_entry nicht gelöscht werden soll, wird er in ein neues Segment kopiert
                    if(!log_entry->to_delete) {
                        // wenn im neuen Segment kein Platz mehr für log_entry ist, wird es an used_segments angehangen und ein neues wird geholt
                        if(SEGMENT_SIZE - cleaner_seg->used_space < log_entry_size) {
                            append_cleaner_segment_to_used_segments(pmem, cleaner_seg->id);
                            cleaner_seg = get_new_cleaner_segment(pmem);
                        }

                        // log_entry in neues Segment kopieren
                        pmem->memcpy_fn((uint8_t*)cleaner_seg->log_entries + cleaner_seg->used_space, log_entry, log_entry_size, 0);
                        pmo_t offset = pointer_to_offset(pmem, (uint8_t*)cleaner_seg->log_entries + cleaner_seg->used_space);
                        cleaner_seg->used_space += log_entry_size;
                        pmem->persist(&cleaner_seg->used_space, sizeof(size_t));
                        
                        add_to_hash_table(log_entry->id, offset);
                    }

                    pos += log_entry_size;
                }

                uint64_t seg_id = curr_seg->id;
                curr_seg = offset_to_pointer(pmem, curr_seg->next_segment);
                append_cleaned_segment_to_free_segments(pmem, seg_id);
            } else {
                curr_seg = offset_to_pointer(pmem, curr_seg->next_segment);
            }
        }
    }

    return NULL;
}

int to_clean(segment_t *segment) {
    return segment->used_space < SEGMENT_SIZE/2 ? 1 : 0;
}

segment_t * get_new_cleaner_segment(pmem_t *pmem) {
    log_t *log = pmem->log;

    // leeres Segment an cleaner_segments anhängen
    segment_t *last_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->last_cleaner_segment);
    if(!last_cleaner_segment) {
        log->cleaner_segments = log->free_segments;
    } else {
        last_cleaner_segment->next_segment = log->free_segments;
        pmem->persist(&last_cleaner_segment->next_segment, sizeof(pmo_t));
    }
    log->last_cleaner_segment = log->free_segments;

    // neues Segment aus free_segments entfernen
    log->free_segments = ((segment_t*)offset_to_pointer(pmem, log->free_segments))->next_segment;
    pmem->persist(log, sizeof(log_t));

    // Nachfolger von last_cleaner_segment auf NULL setzen
    last_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->last_cleaner_segment);
    last_cleaner_segment->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&last_cleaner_segment->next_segment, sizeof(pmo_t));

    return last_cleaner_segment;
}

void append_cleaner_segment_to_used_segments(pmem_t *pmem, uint64_t id) {
    log_t *log = pmem->log;

    segment_t *curr_seg = offset_to_pointer(pmem, log->cleaner_segments);
    segment_t *prev_seg = NULL;

    // richtiges Segment und Vorgänger finden
    for(;curr_seg->id != id; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        prev_seg = curr_seg;
    }

    // curr_seg an used_segments anhängen
    segment_t *last_used_segment = (segment_t*)offset_to_pointer(pmem, log->last_used_segment);
    if(!last_used_segment) {
        log->used_segments = pointer_to_offset(pmem, curr_seg);
    } else {
        last_used_segment->next_segment = pointer_to_offset(pmem, curr_seg);
        pmem->persist(&last_used_segment->next_segment, sizeof(pmo_t));
    }
    log->last_used_segment = pointer_to_offset(pmem, curr_seg);

    // curr_seg aus cleaner_segments entfernen
    if(((segment_t*)offset_to_pointer(pmem, log->cleaner_segments))->id == id) {
        log->cleaner_segments = curr_seg->next_segment;
    } else {
        prev_seg->next_segment = curr_seg->next_segment;
        pmem->persist(&prev_seg->next_segment, sizeof(pmo_t));
    }
    
    // Nachfolger von last_used_segment auf NULL setzen
    curr_seg->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&curr_seg->next_segment, sizeof(pmo_t));

    pmem->persist(log, sizeof(log_t));

    return;
}

void append_cleaned_segment_to_free_segments(pmem_t *pmem, uint64_t id) {
    log_t *log = pmem->log;

    segment_t *curr_seg = offset_to_pointer(pmem, log->used_segments);
    segment_t *prev_seg = NULL;

    // richtiges Segment und Vorgänger finden
    for(;curr_seg->id != id; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        prev_seg = curr_seg;
    }

    // curr_seg an free_segments anhängen
    segment_t *last_free_segment = (segment_t*)offset_to_pointer(pmem, log->last_free_segment);
    if(!last_free_segment) {
        log->free_segments = pointer_to_offset(pmem, curr_seg);
    } else {
        last_free_segment->next_segment = pointer_to_offset(pmem, curr_seg);
        pmem->persist(&last_free_segment->next_segment, sizeof(pmo_t));
    }
    log->last_free_segment = pointer_to_offset(pmem, curr_seg);

    // curr_seg aus used_segments entfernen
    if(((segment_t*)offset_to_pointer(pmem, log->used_segments))->id == id) {
        log->used_segments = curr_seg->next_segment;
    } else {
        prev_seg->next_segment = curr_seg->next_segment;
        pmem->persist(&prev_seg->next_segment, sizeof(pmo_t));
    }
    
    // Nachfolger von last_free_segment auf NULL setzen
    curr_seg->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&curr_seg->next_segment, sizeof(pmo_t));

    pmem->persist(log, sizeof(log_t));

    return;
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