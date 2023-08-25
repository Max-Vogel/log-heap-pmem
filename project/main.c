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
}

void dump_used_seg_ids(pmem_t *pmem) {
    printf("used_segments:\n");
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->used_segments);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        printf("%ld\n", curr_seg->id);
    }
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
}

void dump_emergency_cleaner_seg_ids(pmem_t *pmem) {
    printf("emergency_cleaner_segments:\n");
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->emergency_cleaner_segments);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        printf("%ld\n", curr_seg->id);
    }
}

int main(void) {
    pmem_t *pmem = malloc(sizeof(pmem_t));
    if(!pmem) {
        return 1;
    }

    char *path = "test";
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

    // char *test_data = "hello world";
    // uint64_t ids[8000];
    // for(int i=0; i<1935; i++) {
    //     ids[i] = palloc(pmem, strlen(test_data) + 1, test_data);
    //     printf("%ld\n", ids[i]);
    // }

    // for(int i=0; i<1000; i++) {
    //     // char* str = get_address(pmem, ids[i]);
    //     char* str = get_address(pmem, i);
    //     // printf("%s\n", str);
    // }

    // for(int i=400; i<430; i++) {
    //     pfree(pmem, i);
    // }


    if(delete_pmem(pmem)) {
        free(pmem);
        return 1;
    }
    free(pmem);

    return 0;
}
