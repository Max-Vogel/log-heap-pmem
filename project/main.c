#include "log_pmem.h"
// #include "hash_table.h"

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

int main(void) {
    pmem_t *pmem = malloc(sizeof(pmem_t));
    if(!pmem) {
        return 1;
    }

    char *path = "test";
    if(init_pmem(pmem, path)) {
        return 1;
    }

    // dump_free_seg_ids(pmem);
    // dump_head_seg_ids(pmem);
    // dump_used_seg_ids(pmem);

    // char *test_data = "hello world";
    // uint64_t ids[100];
    // for(int i=0; i<100; i++) {
    //     ids[i] = palloc(pmem, strlen(test_data) + 1, test_data);
    //     printf("%ld\n", ids[i]);
    // }

    // for(int i=0; i<100; i++) {
    //     // char* str = get_address(pmem, ids[i]);
    //     char* str = get_address(pmem, i);
    //     printf("%s\n", str);
    // }

    printf("%s\n", (char*)get_address(pmem, 499));
    
    // pfree(pmem, 500);


    if(delete_pmem(pmem)) {
        return 1;
    }

    return 0;
}
