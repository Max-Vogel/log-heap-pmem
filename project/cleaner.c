#include "cleaner.h"
#include "hash_table.h"

int init_cleaner(pmem_t *pmem) {
    // printf("init_cleaner\n");
    pmem->currently_cleaned_segments = g_hash_table_new(g_direct_hash, g_direct_equal);
    
    for(int i=0; i<CLEANER_THREAD_COUNT; i++) {
        if(pthread_create(&pmem->cleaner_thread_refs[i], NULL, &clean, pmem)) {
            perror("pthread_create");
            return 1;
        }
    }

    return 0;
}

int terminate_cleaner(pmem_t *pmem) {
    // printf("terminate_cleaner\n");
    for(int i=0; i<CLEANER_THREAD_COUNT; i++) {
        pthread_cancel(pmem->cleaner_thread_refs[i]);
    }

    for(int i=0; i<CLEANER_THREAD_COUNT; i++) {
        pthread_join(pmem->cleaner_thread_refs[i], NULL);
    }

    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->cleaner_segments);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        if(curr_seg->used_space == 0) {
            append_cleaner_segment_to_free_segments(pmem, curr_seg->id);
        }
    }

    append_all_cleaner_segments_to_used_segments(pmem);

    g_hash_table_destroy(pmem->currently_cleaned_segments);

    return 0;
}

void * clean(void *param) {
    // printf("clean\n");
    pmem_t *pmem = (pmem_t*)param;
    log_t *log = pmem->log;

    while(wait_to_start_cleaning(pmem)) {
        sleep(1);
    }
    
    segment_t *cleaner_seg = NULL;
    // cleaner_seg = get_new_cleaner_segment(pmem);
    while(!(cleaner_seg = get_new_cleaner_segment(pmem))) {
        sleep(1);
    }
    size_t cleaner_seg_pos = 0;
    // printf("%ld\n", log->cleaner_segments);
    while(1) {
        pthread_testcancel();
        segment_t *curr_seg = offset_to_pointer(pmem, log->used_segments);
        // alle Segmente durchgehen
        while(curr_seg) {
            pthread_testcancel();
            if(to_clean(curr_seg) && test_and_set_curr_cleaned(pmem, curr_seg)) {
                // printf("cleaning segment %ld\n", curr_seg->id);
                pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
                if(curr_seg->used_space != 0) {
                    int pos = 0;
                    // alle log_entries durchgehen
                    while(pos < SEGMENT_SIZE - sizeof(segment_t) - sizeof(log_entry_t)) {
                        log_entry_t *log_entry = (log_entry_t*)((uint8_t*)curr_seg->log_entries + sizeof(uint8_t) * pos);
                        size_t log_entry_size = sizeof(log_entry_t) + sizeof(uint8_t) * log_entry->length;

                        // wenn das Ende des Segments leer ist, das nächste Segment prüfen
                        if(log_entry->length == 0) {
                            break;
                        }

                        pthread_mutex_lock(&pmem->update_move_mutex);
                        log_entry_t *log_entry_from_hash_table = offset_to_pointer(pmem, get_from_hash_table(pmem, log_entry->id));

                        // wenn log_entry nicht gelöscht werden soll, wird er in ein neues Segment kopiert
                        if(!log_entry->to_delete && log_entry->version >= log_entry_from_hash_table->version) {
                            // wenn im neuen Segment kein Platz mehr für log_entry ist, wird es an used_segments angehangen und ein neues wird geholt
                            if(SEGMENT_SIZE - sizeof(segment_t) - cleaner_seg_pos < log_entry_size) {
                                append_cleaner_segment_to_used_segments(pmem, cleaner_seg->id);
                                while(!(cleaner_seg = get_new_cleaner_segment(pmem))) {
                                    sleep(1);
                                }
                                cleaner_seg_pos = 0;
                            }

                            // log_entry in neues Segment kopieren
                            log_entry_t *log_entry_start = (log_entry_t*)((uint8_t*)cleaner_seg->log_entries + cleaner_seg_pos);
                            pmem->memcpy_fn(log_entry_start, log_entry, log_entry_size, 0);
                            pmo_t offset = pointer_to_offset(pmem, log_entry_start);
                            cleaner_seg->used_space += log_entry_size;
                            pmem->persist(&cleaner_seg->used_space, sizeof(size_t));
                            cleaner_seg_pos += log_entry_size;
                            
                            add_to_hash_table(pmem, log_entry->id, offset);
                        }

                        pthread_mutex_unlock(&pmem->update_move_mutex);
                        pos += log_entry_size;
                    }
                }

                uint64_t seg_id = curr_seg->id;
                segment_t *cleaned = curr_seg;
                pthread_mutex_lock(&pmem->used_segs_mutex);
                curr_seg = offset_to_pointer(pmem, curr_seg->next_segment);
                pthread_mutex_unlock(&pmem->used_segs_mutex);
                append_cleaned_segment_to_free_segments(pmem, seg_id);
                unset_curr_cleaned(pmem, cleaned);
                pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            } else {
                pthread_mutex_lock(&pmem->used_segs_mutex);
                curr_seg = offset_to_pointer(pmem, curr_seg->next_segment);
                pthread_mutex_unlock(&pmem->used_segs_mutex);
            }
        }
    }

    return NULL;
}

int wait_to_start_cleaning(pmem_t *pmem) {
    return pmem->log->used_segments_count < (pmem->size / SEGMENT_SIZE) / 2;
}

int to_clean(segment_t *segment) {
    return segment->used_space < SEGMENT_SIZE / 2;// ? 1 : 0;
}

int test_and_set_curr_cleaned(pmem_t *pmem, segment_t *segment) {
    // printf("test_and_set_curr_cleaned\n");
    pthread_mutex_lock(&pmem->curr_cleaned_mutex);
    int ret = g_hash_table_contains(pmem->currently_cleaned_segments, GSIZE_TO_POINTER(segment->id));
    if(!ret) {
        g_hash_table_add(pmem->currently_cleaned_segments, GSIZE_TO_POINTER(segment->id));
    }
    pthread_mutex_unlock(&pmem->curr_cleaned_mutex);
    return !ret;
}

void unset_curr_cleaned(pmem_t *pmem, segment_t *segment) {
    g_hash_table_remove(pmem->currently_cleaned_segments, GSIZE_TO_POINTER(segment->id));
}

segment_t * get_new_cleaner_segment(pmem_t *pmem) {
    // printf("get_new_cleaner_segment ");
    log_t *log = pmem->log;

    if(log->free_segments == 0) {
        return get_emergency_cleaner_segment(pmem);
    }

    // leeres Segment an cleaner_segments anhängen
    pthread_mutex_lock(&pmem->cleaner_segs_mutex);
    pthread_mutex_lock(&pmem->free_segs_mutex);
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
    if(log->free_segments == 0) {
        log->last_free_segment = pointer_to_offset(pmem, NULL);
    }
    pthread_mutex_unlock(&pmem->free_segs_mutex);

    // Nachfolger von last_cleaner_segment auf NULL setzen
    last_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->last_cleaner_segment);
    last_cleaner_segment->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&last_cleaner_segment->next_segment, sizeof(pmo_t));
    pthread_mutex_unlock(&pmem->cleaner_segs_mutex);

    // printf("%ld\n", last_cleaner_segment->id);
    return last_cleaner_segment;
}

segment_t * get_emergency_cleaner_segment(pmem_t *pmem) {
    // printf("get_emergency_cleaner_segment\n");
    log_t *log = pmem->log;
    
    if(!log->emergency_cleaner_segments) {
        fprintf(stderr, "no memory space left\n");
        return NULL;
    }

    // Emergency Segment an cleaner_segments anhängen
    pthread_mutex_lock(&pmem->cleaner_segs_mutex);
    pthread_mutex_lock(&pmem->ermergency_segs_mutex);
    segment_t *last_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->last_cleaner_segment);
    if(!last_cleaner_segment) {
        log->cleaner_segments = log->emergency_cleaner_segments;
    } else {
        last_cleaner_segment->next_segment = log->emergency_cleaner_segments;
        pmem->persist(&last_cleaner_segment->next_segment, sizeof(pmo_t));
    }
    log->last_cleaner_segment = log->emergency_cleaner_segments;

    // neues Segment aus emergency_cleaner_segments entfernen
    log->emergency_cleaner_segments = ((segment_t*)offset_to_pointer(pmem, log->emergency_cleaner_segments))->next_segment;
    if(log->emergency_cleaner_segments == 0) {
        log->last_emergency_cleaner_segment = pointer_to_offset(pmem, NULL);
    }
    pmem->persist(log, sizeof(log_t));
    
    log->current_emergency_cleaner_segments_count--;
    pmem->persist(&log->current_emergency_cleaner_segments_count, sizeof(size_t));
    pthread_mutex_unlock(&pmem->ermergency_segs_mutex);

    // Nachfolger von last_cleaner_segment auf NULL setzen
    last_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->last_cleaner_segment);
    last_cleaner_segment->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&last_cleaner_segment->next_segment, sizeof(pmo_t));
    pthread_mutex_unlock(&pmem->cleaner_segs_mutex);

    return last_cleaner_segment;
}

void refill_emergency_cleaner_segment(pmem_t *pmem, segment_t *curr_seg) {
    // printf("refill_emergency_cleaner_segment\n");
    log_t *log = pmem->log;

    // curr_seg an emergency_cleaner_segments anhängen
    pthread_mutex_lock(&pmem->ermergency_segs_mutex);
    segment_t *last_emergency_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->last_emergency_cleaner_segment);
    if(!last_emergency_cleaner_segment) {
        log->emergency_cleaner_segments = pointer_to_offset(pmem, curr_seg);
    } else {
        last_emergency_cleaner_segment->next_segment = pointer_to_offset(pmem, curr_seg);
        pmem->persist(&last_emergency_cleaner_segment->next_segment, sizeof(pmo_t));
    }
    log->last_emergency_cleaner_segment = pointer_to_offset(pmem, curr_seg);

    log->current_emergency_cleaner_segments_count++;
    pmem->persist(log, sizeof(log_t));
    pthread_mutex_unlock(&pmem->ermergency_segs_mutex);

    // emergency_segment wird nicht aus cleaner_segments entfernt, muss von aufrufender Funktion gemacht werden

    return;
}

void append_cleaner_segment_to_used_segments(pmem_t *pmem, uint64_t id) {
    // printf("append_cleaner_segment_to_used_segments\n");
    log_t *log = pmem->log;

    pthread_mutex_lock(&pmem->cleaner_segs_mutex);
    segment_t *curr_seg = offset_to_pointer(pmem, log->cleaner_segments);
    segment_t *prev_seg = NULL;

    // richtiges Segment und Vorgänger finden
    for(;curr_seg->id != id; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        prev_seg = curr_seg;
    }

    // curr_seg an used_segments anhängen
    pthread_mutex_lock(&pmem->used_segs_mutex);
    segment_t *last_used_segment = (segment_t*)offset_to_pointer(pmem, log->last_used_segment);
    if(!last_used_segment) {
        log->used_segments = pointer_to_offset(pmem, curr_seg);
    } else {
        last_used_segment->next_segment = pointer_to_offset(pmem, curr_seg);
        pmem->persist(&last_used_segment->next_segment, sizeof(pmo_t));
    }
    log->last_used_segment = pointer_to_offset(pmem, curr_seg);

    // curr_seg aus cleaner_segments entfernen
    segment_t *first_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->cleaner_segments);
    segment_t *last_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->last_cleaner_segment);
    if(first_cleaner_segment->id == id && last_cleaner_segment->id == id) {
        log->cleaner_segments = pointer_to_offset(pmem, NULL);
        log->last_cleaner_segment = pointer_to_offset(pmem, NULL);
    } else if (first_cleaner_segment->id == id) {
        log->cleaner_segments = curr_seg->next_segment;
    } else {
        if(last_cleaner_segment->id == id) {
            log->last_cleaner_segment = pointer_to_offset(pmem, prev_seg);
        }
        prev_seg->next_segment = curr_seg->next_segment;
        pmem->persist(&prev_seg->next_segment, sizeof(pmo_t));
    }
    pthread_mutex_unlock(&pmem->cleaner_segs_mutex);
    
    // Nachfolger von last_used_segment auf NULL setzen
    curr_seg->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&curr_seg->next_segment, sizeof(pmo_t));
    pthread_mutex_unlock(&pmem->used_segs_mutex);

    pmem->persist(log, sizeof(log_t));

    return;
}

void append_cleaned_segment_to_free_segments(pmem_t *pmem, uint64_t id) {
    // printf("append_cleaned_segment_to_free_segments %ld\n", id);
    log_t *log = pmem->log;

    pthread_mutex_lock(&pmem->used_segs_mutex);
    segment_t *curr_seg = offset_to_pointer(pmem, log->used_segments);
    segment_t *prev_seg = NULL;

    // richtiges Segment und Vorgänger finden
    for(;curr_seg->id != id; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        prev_seg = curr_seg;
    }

    // zuerst emergency_cleaner_segment auffüllen
    if(log->current_emergency_cleaner_segments_count < EMERGENCY_CLEANER_SEGMENT_COUNT) {
        refill_emergency_cleaner_segment(pmem, curr_seg);
    } else {
        // curr_seg an free_segments anhängen
        pthread_mutex_lock(&pmem->free_segs_mutex);
        segment_t *last_free_segment = (segment_t*)offset_to_pointer(pmem, log->last_free_segment);
        if(!last_free_segment) {
            log->free_segments = pointer_to_offset(pmem, curr_seg);
        } else {
            last_free_segment->next_segment = pointer_to_offset(pmem, curr_seg);
            pmem->persist(&last_free_segment->next_segment, sizeof(pmo_t));
        }
        log->last_free_segment = pointer_to_offset(pmem, curr_seg);
    }

    // curr_seg aus used_segments entfernen
    segment_t *first_used_segment = (segment_t*)offset_to_pointer(pmem, log->used_segments);
    segment_t *last_used_segment = (segment_t*)offset_to_pointer(pmem, log->last_used_segment);
    if(first_used_segment->id == id && last_used_segment->id == id) {
        log->used_segments = pointer_to_offset(pmem, NULL);
        log->last_used_segment = pointer_to_offset(pmem, NULL);
    } else if (first_used_segment->id == id) {
        log->used_segments = curr_seg->next_segment;
    } else {
        if(last_used_segment->id == id) {
            log->last_used_segment = pointer_to_offset(pmem, prev_seg);
        }
        prev_seg->next_segment = curr_seg->next_segment;
        pmem->persist(&prev_seg->next_segment, sizeof(pmo_t));
    }   
    log->used_segments_count--;
    pthread_mutex_unlock(&pmem->used_segs_mutex);
    
    // Nachfolger von last_free_segment auf NULL setzen
    curr_seg->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&curr_seg->next_segment, sizeof(pmo_t));
    pthread_mutex_unlock(&pmem->free_segs_mutex);

    pmem->persist(log, sizeof(log_t));

    return;
}

void append_cleaner_segment_to_free_segments(pmem_t *pmem, uint64_t id) {
    // printf("append_cleaner_segment_to_free_segments %ld\n", id);
    log_t *log = pmem->log;

    pthread_mutex_lock(&pmem->cleaner_segs_mutex);
    segment_t *curr_seg = offset_to_pointer(pmem, log->cleaner_segments);
    segment_t *prev_seg = NULL;

    // richtiges Segment und Vorgänger finden
    for(;curr_seg->id != id; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        prev_seg = curr_seg;
    }

    // zuerst emergency_cleaner_segment auffüllen
    if(log->current_emergency_cleaner_segments_count < EMERGENCY_CLEANER_SEGMENT_COUNT) {
        refill_emergency_cleaner_segment(pmem, curr_seg);
    } else {
        // curr_seg an free_segments anhängen
        pthread_mutex_lock(&pmem->free_segs_mutex);
        segment_t *last_free_segment = (segment_t*)offset_to_pointer(pmem, log->last_free_segment);
        if(!last_free_segment) {
            log->free_segments = pointer_to_offset(pmem, curr_seg);
        } else {
            last_free_segment->next_segment = pointer_to_offset(pmem, curr_seg);
            pmem->persist(&last_free_segment->next_segment, sizeof(pmo_t));
        }
        log->last_free_segment = pointer_to_offset(pmem, curr_seg);
    }

    // curr_seg aus cleaner_segments entfernen
    segment_t *first_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->cleaner_segments);
    segment_t *last_cleaner_segment = (segment_t*)offset_to_pointer(pmem, log->last_cleaner_segment);
    if(first_cleaner_segment->id == id && last_cleaner_segment->id == id) {
        log->cleaner_segments = pointer_to_offset(pmem, NULL);
        log->last_cleaner_segment = pointer_to_offset(pmem, NULL);
    } else if (first_cleaner_segment->id == id) {
        log->cleaner_segments = curr_seg->next_segment;
    } else {
        if(last_cleaner_segment->id == id) {
            log->last_cleaner_segment = pointer_to_offset(pmem, prev_seg);
        }
        prev_seg->next_segment = curr_seg->next_segment;
        pmem->persist(&prev_seg->next_segment, sizeof(pmo_t));
    }
    pthread_mutex_unlock(&pmem->cleaner_segs_mutex);
    
    // Nachfolger von last_free_segment auf NULL setzen
    curr_seg->next_segment = pointer_to_offset(pmem, NULL);
    pmem->persist(&curr_seg->next_segment, sizeof(pmo_t));
    pthread_mutex_unlock(&pmem->free_segs_mutex);

    pmem->persist(log, sizeof(log_t));

    return;
}

void append_all_cleaner_segments_to_used_segments(pmem_t *pmem) {
    // printf("append_all_cleaner_segments_to_used_segments\n");
    log_t *log = pmem->log;

    // wenn es keine Cleanersegmente gibt
    if(!log->cleaner_segments) {
        // printf("no segments to append\n");
        return;
    }

    // cleaner_segment an used_segments anhängen
    pthread_mutex_lock(&pmem->cleaner_segs_mutex);
    pthread_mutex_lock(&pmem->used_segs_mutex);
    segment_t *last_used_segment = (segment_t*)offset_to_pointer(pmem, log->last_used_segment);
    if(!last_used_segment) {
        log->used_segments = log->cleaner_segments;
    } else {
        last_used_segment->next_segment = log->cleaner_segments;
        pmem->persist(&last_used_segment->next_segment, sizeof(pmo_t));
    }
    log->last_used_segment = log->last_cleaner_segment;

    log->cleaner_segments = pointer_to_offset(pmem, NULL);
    log->last_cleaner_segment = pointer_to_offset(pmem, NULL);
    pthread_mutex_unlock(&pmem->used_segs_mutex);
    pthread_mutex_unlock(&pmem->cleaner_segs_mutex);

    pmem->persist(log, sizeof(log_t));
}

void append_all_cleaner_segments_to_free_segments(pmem_t *pmem) {
    // printf("append_all_cleaner_segments_to_free_segments\n");
    log_t *log = pmem->log;

    // wenn es keine Cleanersegmente gibt
    if(!log->cleaner_segments) {
        // printf("no segments to append\n");
        return;
    }

    // zuerst emergency_cleaner_segment auffüllen
    pthread_mutex_lock(&pmem->cleaner_segs_mutex);
    while(log->current_emergency_cleaner_segments_count < EMERGENCY_CLEANER_SEGMENT_COUNT && log->cleaner_segments) {
        segment_t *cleaner_segment = offset_to_pointer(pmem, log->cleaner_segments);
        refill_emergency_cleaner_segment(pmem, cleaner_segment);
        
        log->cleaner_segments = cleaner_segment->next_segment;

        ((segment_t*)offset_to_pointer(pmem, log->last_emergency_cleaner_segment))->next_segment = pointer_to_offset(pmem, NULL);
        pmem->persist(&log->last_emergency_cleaner_segment, sizeof(pmo_t));
    }

    // cleaner_segments an free_segments anhängen
    pthread_mutex_lock(&pmem->free_segs_mutex);
    segment_t *last_free_segment = (segment_t*)offset_to_pointer(pmem, log->last_free_segment);
    if(!last_free_segment) {
        log->free_segments = log->cleaner_segments;
    } else {
        last_free_segment->next_segment = log->cleaner_segments;
        pmem->persist(&last_free_segment->next_segment, sizeof(pmo_t));
    }
    log->last_free_segment = log->last_cleaner_segment;
    pthread_mutex_unlock(&pmem->free_segs_mutex);

    log->cleaner_segments = pointer_to_offset(pmem, NULL);
    log->last_cleaner_segment = pointer_to_offset(pmem, NULL);
    pthread_mutex_unlock(&pmem->cleaner_segs_mutex);

    pmem->persist(log, sizeof(log_t));
}
