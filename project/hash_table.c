#include "hash_table.h"

void init_hash_table(pmem_t *pmem) {
    pmem->hash_table = g_hash_table_new(g_direct_hash, g_direct_equal);
}

void add_to_hash_table(pmem_t *pmem, uint64_t id, pmo_t offset) {
    g_hash_table_insert(pmem->hash_table, GSIZE_TO_POINTER(id), GSIZE_TO_POINTER(offset));
}

pmo_t get_from_hash_table(pmem_t *pmem, uint64_t id) {
    return GPOINTER_TO_SIZE(g_hash_table_lookup(pmem->hash_table, GSIZE_TO_POINTER(id)));
}

void remove_from_hash_table(pmem_t *pmem, uint64_t id) {
    g_hash_table_remove(pmem->hash_table, (gpointer)&id);
}

void destroy_hash_table(pmem_t *pmem) {
    g_hash_table_destroy(pmem->hash_table);
}

void reconstruct_hash_table(pmem_t *pmem) {
    GHashTable *hash_table = pmem->hash_table;
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->used_segments);
    int pos;

    // used_segments durchgehen
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        pos = 0;
        // alle log_entries durchgehen
        while(pos < SEGMENT_SIZE - sizeof(segment_t) - sizeof(log_entry_t)) {
            log_entry_t *log_entry = (log_entry_t*)((uint8_t*)curr_seg->log_entries + sizeof(uint8_t) * pos);
            size_t log_entry_size = sizeof(log_entry_t) + sizeof(uint8_t) * log_entry->length;

            // wenn das Ende des Segments leer ist, das nächste Segment prüfen
            if(log_entry->length == 0) {
                break;
            }

            // wenn log_entry nicht gelöscht werden soll, wird es wieder in hash_table aufgenommen
            if(!log_entry->to_delete) {
                // wenn schon ein Objekt mit dieser ID in hash_table ist
                if(g_hash_table_contains(hash_table, GSIZE_TO_POINTER(log_entry->id))) {
                    log_entry_t *log_entry_from_hash_table = offset_to_pointer(pmem, get_from_hash_table(pmem, log_entry->id));
                    // wenn die Version von log_entry neuer ist, als die des aktuellen Eintrags aus hash_table
                    if(log_entry->version > log_entry_from_hash_table->version) {
                        add_to_hash_table(pmem, log_entry->id, pointer_to_offset(pmem, log_entry));
                    }
                } else {
                    add_to_hash_table(pmem, log_entry->id, pointer_to_offset(pmem, log_entry));
                }
            }
            
            pos += log_entry_size;
        }
    }

    segment_t *head_seg = offset_to_pointer(pmem, pmem->log->head_segment);
    pos = 0;
    // alle log_entries in head_segment durchgehen
    while(pos < pmem->log->head_position) {
        log_entry_t *log_entry = (log_entry_t*)((uint8_t*)head_seg->log_entries + sizeof(uint8_t) * pos);
        size_t log_entry_size = sizeof(log_entry_t) + sizeof(uint8_t) * log_entry->length;

        // wenn log_entry nicht gelöscht werden soll, wird es wieder in hash_table aufgenommen
        if(!log_entry->to_delete) {
            // wenn schon ein Objekt mit dieser ID in hash_table ist
            if(g_hash_table_contains(hash_table, GSIZE_TO_POINTER(log_entry->id))) {
                log_entry_t *log_entry_from_hash_table = offset_to_pointer(pmem, get_from_hash_table(pmem, log_entry->id));
                // wenn die Version von log_entry neuer ist, als die des aktuellen Eintrags aus hash_table
                if(log_entry->version > log_entry_from_hash_table->version) {
                    add_to_hash_table(pmem, log_entry->id, pointer_to_offset(pmem, log_entry));
                }
            } else {
                add_to_hash_table(pmem, log_entry->id, pointer_to_offset(pmem, log_entry));
            }
        }
        
        pos += log_entry_size;
    }
}

// TODO: implementieren
void persist_hash_table(pmem_t *pmem) {

}