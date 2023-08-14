#include "log.h"

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
    log->head_segment = pointer_to_offset(pmem, NULL);
    log->free_segments = pointer_to_offset(pmem, curr_seg);
    log->used_segments = pointer_to_offset(pmem, NULL);
    log->last_used_segment = pointer_to_offset(pmem, NULL);
    log->cleaner_segments = pointer_to_offset(pmem, NULL);
    log->last_cleaner_segment = pointer_to_offset(pmem, NULL);
    log->current_id = 0;
    log->head_position = 0;
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

    log->last_free_segment = pointer_to_offset(pmem, prev_seg);
    pmem->persist(&log->last_free_segment, sizeof(pmo_t));

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

pmo_t append_to_log(pmem_t *pmem, log_entry_t *log_entry) {
    log_t *log = pmem->log;
    size_t log_entry_size = sizeof(log_entry_t) + sizeof(uint8_t) * log_entry->length;
    segment_t* head_seg = offset_to_pointer(pmem, log->head_segment);

    // wenn im head_segment nicht mehr genug Platz für den neuen Eintrag ist
    if(SEGMENT_SIZE - sizeof(segment_t) - log->head_position < log_entry_size) {
        // wenn das Ende des Segments leer ist und noch kleinere Daten als log_entry hineinpassen würden
        if(SEGMENT_SIZE - sizeof(segment_t) - log->head_position >= sizeof(log_entry_t)) {
            log_entry_t *unused_space = (log_entry_t*)((uint8_t*)head_seg->log_entries + log->head_position);
            unused_space->length = 0;
            pmem->persist(&unused_space->length, sizeof(size_t));
        }
        append_head_to_used_segments_and_get_new_head_segment(pmem);
        head_seg = offset_to_pointer(pmem, log->head_segment);
    }

    // log_entry ins Log schreiben und used_space im Segment erhöhen
    log_entry_t *log_entry_start = (log_entry_t*)((uint8_t*)head_seg->log_entries + sizeof(uint8_t) * log->head_position); 
    pmem->memcpy_fn(log_entry_start, log_entry, log_entry_size, 0);
    pmo_t pmem_offset = pointer_to_offset(pmem, log_entry_start);
    head_seg->used_space += log_entry_size;
    pmem->persist(&head_seg->used_space, sizeof(size_t));
    log->head_position += log_entry_size;
    pmem->persist(&log->head_position, sizeof(size_t));
    
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

    log->head_position = 0;
    pmem->persist(&log->head_position, sizeof(size_t));
}

// TODO: was besseres ausdenken, vielleicht uuid?
uint64_t gen_id(pmem_t *pmem) {
    uint64_t id = pmem->log->current_id++;
    if(g_hash_table_contains(pmem->hash_table, GSIZE_TO_POINTER(id))) {
        id = pmem->log->current_id++;
    }
    return id;
}
