#include "cleaner.h"
#include "hash_table.h"

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
    size_t cleaner_seg_pos = 0;
    
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
                while(pos < SEGMENT_SIZE - sizeof(segment_t) - sizeof(log_entry_t)) {
                    log_entry_t *log_entry = (log_entry_t*)((uint8_t*)curr_seg->log_entries + sizeof(uint8_t) * pos);
                    size_t log_entry_size = sizeof(log_entry_t) + sizeof(uint8_t) * log_entry->length;

                    // wenn das Ende des Segments leer ist, das nächste Segment prüfen
                    if(log_entry->length == 0) {
                        break;
                    }

                    // wenn log_entry nicht gelöscht werden soll, wird er in ein neues Segment kopiert
                    if(!log_entry->to_delete) {
                        // wenn im neuen Segment kein Platz mehr für log_entry ist, wird es an used_segments angehangen und ein neues wird geholt
                        if(SEGMENT_SIZE - sizeof(segment_t) - cleaner_seg_pos < log_entry_size) {
                            append_cleaner_segment_to_used_segments(pmem, cleaner_seg->id);
                            cleaner_seg = get_new_cleaner_segment(pmem);
                            cleaner_seg_pos = 0;
                        }

                        // log_entry in neues Segment kopieren
                        log_entry_t *log_entry_start = (uint8_t*)cleaner_seg->log_entries + cleaner_seg_pos;
                        pmem->memcpy_fn(log_entry_start, log_entry, log_entry_size, 0);
                        pmo_t offset = pointer_to_offset(pmem, log_entry_start);
                        cleaner_seg->used_space += log_entry_size;
                        pmem->persist(&cleaner_seg->used_space, sizeof(size_t));
                        cleaner_seg_pos += log_entry_size;
                        
                        add_to_hash_table(pmem, log_entry->id, offset);
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
