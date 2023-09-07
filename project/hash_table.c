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
    g_hash_table_remove(pmem->hash_table, GSIZE_TO_POINTER(id));
}

void destroy_hash_table(pmem_t *pmem) {
    g_hash_table_destroy(pmem->hash_table);
}

int hash_table_contains(pmem_t *pmem, uint64_t id) {
    return g_hash_table_contains(pmem->hash_table, GSIZE_TO_POINTER(id));
}

void reconstruct_hash_table(pmem_t *pmem) {
    // printf("reconstruct_hash_table\n");
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
                if(hash_table_contains(pmem, log_entry->id)) {
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
    if(!head_seg) {
        return;
    }
    pos = 0;
    // alle log_entries in head_segment durchgehen
    while(pos < pmem->log->head_position) {
        log_entry_t *log_entry = (log_entry_t*)((uint8_t*)head_seg->log_entries + sizeof(uint8_t) * pos);
        size_t log_entry_size = sizeof(log_entry_t) + sizeof(uint8_t) * log_entry->length;

        // wenn log_entry nicht gelöscht werden soll, wird es wieder in hash_table aufgenommen
        if(!log_entry->to_delete) {
            // wenn schon ein Objekt mit dieser ID in hash_table ist
            if(hash_table_contains(pmem, log_entry->id)) {
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

int get_persisted_hash_table(pmem_t *pmem) {
    // printf("get_persisted_hash_table\n");
    char *path = malloc(strlen(pmem->path_to_pmem) + strlen("_hash_table") + 1);
    strcpy(path, pmem->path_to_pmem);
    strcat(path, "_hash_table");
    if(access(path, F_OK)) {
        free(path);
        return 1;
    }
    free(path);

    hash_pmem_t *hpmem = malloc(sizeof(hash_pmem_t));
    if(!hpmem) {
        fprintf(stderr, "failed to load persisted hash table 1\n");
        return 1;
    }

    if(init_hash_pmem(pmem, hpmem)) {
        fprintf(stderr, "failed to load persisted hash table 2\n");
        free(hpmem);
        return 1;
    }
    
    if(hpmem->hash_meta->succesfully_persisted == 0) {
        delete_hash_pmem(hpmem);
        free(hpmem);
        return 1;
    }
    
    // über Key-Value-Paare iterieren und zum Hashtable hinzufügen
    for(int i=0; i<hpmem->hash_meta->counter; i++) {
        kvp_t *pair = &hpmem->hash_meta->data[i]; //+ sizeof(kvp_t) * i;
        add_to_hash_table(pmem, pair->key, pair->value);
    }

    // remove(path);

    delete_hash_pmem(hpmem);
    free(hpmem);

    return 0;
}

int persist_hash_table(pmem_t *pmem) {
    // printf("persist_hash_table\n");
    if(!g_hash_table_size(pmem->hash_table)) {
        return 0;
    }

    hash_pmem_t *hpmem = malloc(sizeof(hash_pmem_t));
    if(!hpmem) {
        fprintf(stderr, "failed to persist hash table\n");
        return 1;
    }

    if(init_hash_pmem(pmem, hpmem)) {
        fprintf(stderr, "failed to persist hash table\n");
        free(hpmem);
        return 1;
    }

    // succesfully_persisted und counter mit 0 initialisieren
    hpmem->hash_meta->succesfully_persisted = 0;
    hpmem->persist(&hpmem->hash_meta->succesfully_persisted, sizeof(uint8_t));

    hpmem->hash_meta->counter = 0;
    hpmem->persist(&hpmem->hash_meta->counter, sizeof(size_t));

    // alle Key-Value-Paare persistieren
    g_hash_table_foreach(pmem->hash_table, persist_key_value_pair, hpmem);
    hpmem->persist(hpmem->hash_meta, hpmem->size);

    hpmem->hash_meta->succesfully_persisted = 1;
    hpmem->persist(&hpmem->hash_meta->succesfully_persisted, sizeof(uint8_t));

    delete_hash_pmem(hpmem);
    free(hpmem);

    return 0;
}

void persist_key_value_pair(gpointer key, gpointer value, gpointer user_data) {
    hash_pmem_t *hpmem = user_data;
    
    // Key-Value-Paare werden einfach in den speicher geschrieben, nicht als Hashtable gespeichert
    kvp_t *pair = &hpmem->hash_meta->data[hpmem->hash_meta->counter]; //+ sizeof(kvp_t) * hpmem->hash_meta->counter;
    pair->key = (uint64_t)key;
    pair->value = (pmo_t)value;

    hpmem->hash_meta->counter++;
}

int init_hash_pmem(pmem_t *pmem, hash_pmem_t *hpmem) { 
    char *path = malloc(strlen(pmem->path_to_pmem) + strlen("_hash_table") + 1);
    strcpy(path, pmem->path_to_pmem);
    strcat(path, "_hash_table");
    
    if(!access(path, F_OK)) {
        // wenn die Datei schon existiert -> öffnen
        hpmem->fd = open(path, O_RDWR);
        if(hpmem->fd < 0) {
            perror("open");
            free(path);
            return 1;
        }
    } else {
        // wenn die Datei noch nicht existiert -> erstellen und Größe setzen
        hpmem->fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if(hpmem->fd < 0) {
            perror("open");
            free(path);
            return 1;
        }
    }

    // beim persistieren die Größe der Datei setzen
    size_t hash_table_size = g_hash_table_size(pmem->hash_table);
    if(hash_table_size != 0) {
        size_t file_size = sizeof(hash_meta_t) + sizeof(kvp_t) * hash_table_size;
        if(ftruncate(hpmem->fd, file_size)) {
            perror("ftruncate");
            return 1;
        }
    }
    free(path);

    struct pmem2_config *cfg;
    if(pmem2_config_new(&cfg)) {
        pmem2_perror("pmem2_config_new");
        err_config_hash(hpmem);
        return 1;
	}

    if(pmem2_config_set_required_store_granularity(cfg, PMEM2_GRANULARITY_PAGE)) {
        pmem2_perror("pmem2_config_set_required_store_granularity");
        err_granularity_hash(hpmem, cfg);
        return 1;
	}

	struct pmem2_source *src;
    if(pmem2_source_from_fd(&src, hpmem->fd)) {
        pmem2_perror("pmem2_source_from_fd");
        err_granularity_hash(hpmem, cfg);
        return 1;
	}

    if(pmem2_map_new(&hpmem->map, cfg, src)) {
        pmem2_perror("pmem2_map_new");
        err_map_hash(hpmem, cfg, src);
        return 1;
	}

    hpmem->size = pmem2_map_get_size(hpmem->map);
    hpmem->persist = pmem2_get_persist_fn(hpmem->map);
    hpmem->memcpy_fn = pmem2_get_memcpy_fn(hpmem->map);
    hpmem->hash_meta = pmem2_map_get_address(hpmem->map);

    if(pmem2_source_delete(&src)) {
        pmem2_perror("pmem2_source_delete");
        err_src_delete_hash(hpmem, cfg, src);
        return 1;
	}

	if(pmem2_config_delete(&cfg)) {
        pmem2_perror("pmem2_config_delete");
        err_src_delete_hash(hpmem, cfg, src);
        return 1;
	}

    return 0;
}

int delete_hash_pmem(hash_pmem_t *hpmem) {
    int ret = 0;

    if(pmem2_map_delete(&hpmem->map)) {
        pmem2_perror("pmem2_map_delete");
        ret = 1;
	}

	if(close(hpmem->fd)) {
        perror("close");
        ret = 1;
	}

    return ret;
}

void err_config_hash(hash_pmem_t *hpmem) {
    close(hpmem->fd);
}

void err_granularity_hash(hash_pmem_t *hpmem, struct pmem2_config *cfg) {
    pmem2_config_delete(&cfg);
    err_config_hash(hpmem);
}

void err_map_hash(hash_pmem_t *hpmem, struct pmem2_config *cfg, struct pmem2_source *src) {
    pmem2_source_delete(&src);
    err_granularity_hash(hpmem, cfg);
}

void err_src_delete_hash(hash_pmem_t *hpmem, struct pmem2_config *cfg, struct pmem2_source *src) {
    pmem2_map_delete(&hpmem->map);
    err_map_hash(hpmem, cfg, src);
}
