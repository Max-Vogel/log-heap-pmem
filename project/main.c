#include "log_pmem.h"
// #include "hash_table.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

void dump_free_seg_ids(pmem_t *pmem) {
    printf("free_segments:\n");
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->free_segments);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        printf("%ld\n", curr_seg->id);
    }
    printf("last_free_segment: %ld\n", ((segment_t*)offset_to_pointer(pmem, pmem->log->last_free_segment))->id);
}

void dump_used_seg_ids(pmem_t *pmem) {
    printf("used_segments:\n");
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->used_segments);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        printf("%ld\n", curr_seg->id);
    }
    printf("last_used_segment: %ld\n", ((segment_t*)offset_to_pointer(pmem, pmem->log->last_used_segment))->id);
}

void dump_head_seg_ids(pmem_t *pmem) {
    printf("head_segment:\n");
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->head_segment);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        printf("%ld\n", curr_seg->id);
    }
}

void dump_cleaner_seg_ids(pmem_t *pmem) {
    printf("cleaner_segments:\n");
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->cleaner_segments);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        printf("%ld\n", curr_seg->id);
    }
    // printf("last_cleaner_segment: %ld\n", ((segment_t*)offset_to_pointer(pmem, pmem->log->last_cleaner_segment))->id);
}

void dump_emergency_cleaner_seg_ids(pmem_t *pmem) {
    printf("emergency_cleaner_segments:\n");
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->emergency_cleaner_segments);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        printf("%ld\n", curr_seg->id);
    }
    printf("last_emergency_cleaner_segment: %ld\n", ((segment_t*)offset_to_pointer(pmem, pmem->log->last_emergency_cleaner_segment))->id);
}

void random_string(char *dest, size_t size) {
    if(!size) {
        return;
    }
    char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t i=0;
    
    for(; i<size-1; i++) {
        int index = rand() % (sizeof(chars) - 1);
        dest[i] = chars[index];
    }

    dest[i] = '\0';
}

int main(void) {
    pmem_t *pmem = malloc(sizeof(pmem_t));
    if(!pmem) {
        return 1;
    }

    char *path = "pmem";
    if(init_pmem(pmem, path)) {
        free(pmem);
        return 1;
    }

    sleep(1);

    printf("\n");
    dump_free_seg_ids(pmem);
    dump_head_seg_ids(pmem);
    dump_used_seg_ids(pmem);
    dump_cleaner_seg_ids(pmem);
    dump_emergency_cleaner_seg_ids(pmem);
    printf("\n");

    srand(time(NULL));
    // size_t size= rand() % (1U << 10);
    // char *str = malloc(size);
    // random_string(str, size);
    // if(str) {
    //     printf("%s\n", str);
    // }
    // free(str);

    // for(int i=0; i<10; i++) {
    //     size_t size = 0;
    //     while(!size) {
    //         size = rand() % (1U << 10); // 1024
    //     }

    //     char *str = malloc(size);
    //     if(!str) {
    //         fprintf(stderr, "malloc failed\n");
    //         return 1;
    //     }

    //     random_string(str, size);
    //     uint64_t id = palloc(pmem, size, str);
    //     printf("%ld\n", id);
    // }

    // char *test_data = "hello world";
    // uint64_t ids[8000];
    // for(int i=0; i<10; i++) {
    //     ids[i] = palloc(pmem, strlen(test_data) + 1, test_data);
    //     printf("%ld\n", ids[i]);
    // }

    // for(int i=0; i<10; i++) {
    //     // char* str = get_address(pmem, ids[i]);
    //     char* str = get_address(pmem, i);
    //     printf("%p\n", str);
    // }

    // for(int i=0; i<10; i++) {
    //     pfree(pmem, i);
    // }


    if(delete_pmem(pmem)) {
        free(pmem);
        return 1;
    }
    free(pmem);

    return 0;
}
