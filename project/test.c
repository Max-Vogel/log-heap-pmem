#include <glib.h>
#include "log_pmem.h"
#include "log.h"
#include "cleaner.h"
#include "hash_table.h"


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
    printf("last_cleaner_segment: %ld\n", ((segment_t*)offset_to_pointer(pmem, pmem->log->last_cleaner_segment))->id);
}

void dump_emergency_cleaner_seg_ids(pmem_t *pmem) {
    printf("emergency_cleaner_segments:\n");
    segment_t *curr_seg = offset_to_pointer(pmem, pmem->log->emergency_cleaner_segments);
    for(;curr_seg; curr_seg = offset_to_pointer(pmem, curr_seg->next_segment)) {
        printf("%ld\n", curr_seg->id);
    }
    printf("last_emergency_cleaner_segment: %ld\n", ((segment_t*)offset_to_pointer(pmem, pmem->log->last_emergency_cleaner_segment))->id);
}


static void setup(pmem_t *pmem, gconstpointer user_data) {
    char *path = "test_log_pmem";

    if(!access(path, F_OK)) {
        remove(path);
    }

    if(!access("test_log_pmem_hash_table", F_OK)) {
        remove("test_log_pmem_hash_table");
    }
    
    int fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if(fd < 0) {
        perror("open");
        return;
    }

    if(ftruncate(fd, (1U << 10) * 100)) { // 100 KiB
        perror("ftruncate");
        close(fd);
        return;
    }

    close(fd);
    
    init_pmem(pmem, path);
}

static void tear_down(pmem_t *pmem, gconstpointer user_data) {
    delete_pmem(pmem);
    remove("test_log_pmem");
    remove("test_log_pmem_hash_table");
}

static int get_head_seg_count(pmem_t *pmem) {
    int head_seg_count = 0;
    segment_t *head_seg = offset_to_pointer(pmem, pmem->log->head_segment);
    for(;head_seg; head_seg = offset_to_pointer(pmem, head_seg->next_segment)) {
        head_seg_count++;
    }
    return head_seg_count;
}

static int get_free_seg_count(pmem_t *pmem) {
    int free_seg_count = 0;
    segment_t *free_seg = offset_to_pointer(pmem, pmem->log->free_segments);
    for(;free_seg; free_seg = offset_to_pointer(pmem, free_seg->next_segment)) {
        free_seg_count++;
    }
    return free_seg_count;
}

static int get_used_seg_count(pmem_t *pmem) {
    int used_seg_count = 0;
    segment_t *used_seg = offset_to_pointer(pmem, pmem->log->used_segments);
    for(;used_seg; used_seg = offset_to_pointer(pmem, used_seg->next_segment)) {
        used_seg_count++;
    }
    return used_seg_count;
}

static int get_cleaner_seg_count(pmem_t *pmem) {
    int cleaner_seg_count = 0;
    segment_t *cleaner_seg = offset_to_pointer(pmem, pmem->log->cleaner_segments);
    for(;cleaner_seg; cleaner_seg = offset_to_pointer(pmem, cleaner_seg->next_segment)) {
        cleaner_seg_count++;
    }
    return cleaner_seg_count;
}

static int get_emergency_seg_count(pmem_t *pmem) {
    int emergency_seg_count = 0;
    segment_t *emergency_seg = offset_to_pointer(pmem, pmem->log->emergency_cleaner_segments);
    for(;emergency_seg; emergency_seg = offset_to_pointer(pmem, emergency_seg->next_segment)) {
        emergency_seg_count++;
    }
    return emergency_seg_count;
}

static void random_string(char *dest, size_t size) {
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

static void compare_data(gpointer key, gpointer value, gpointer user_data) {
    pmem_t *pmem = user_data;
    char *data_pmem = get_address(pmem, GPOINTER_TO_SIZE(key));
    g_assert_cmpstr(value, ==, data_pmem);
    free(value);
}


// testet, ob alles richtig initialisiert wird
static void test_1_init(pmem_t *pmem, gconstpointer user_data) {
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 22);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft 3 mal append_head_to_used_segments_and_get_new_head_segment auf
static void test_2_append_head(pmem_t *pmem, gconstpointer user_data) {
    append_head_to_used_segments_and_get_new_head_segment(pmem);
    append_head_to_used_segments_and_get_new_head_segment(pmem);
    append_head_to_used_segments_and_get_new_head_segment(pmem);

    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 19);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 3);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft append_head_to_used_segments_and_get_new_head_segment auf, bis alle Segmente benutzt sind
static void test_3_append_head_all(pmem_t *pmem, gconstpointer user_data) {
    for(int i=0; i<23; i++) {
        append_head_to_used_segments_and_get_new_head_segment(pmem);
    }

    g_assert_cmpint(get_head_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 23);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft 2 mal get_new_cleaner_segment auf
static void test_4_get_cleaner_seg(pmem_t *pmem, gconstpointer user_data) {
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);

    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 20);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 2);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft für jedes freie Segment get_new_cleaner_segment auf
static void test_5_get_cleaner_seg_all(pmem_t *pmem, gconstpointer user_data) {
    for(int i=0; i<22; i++) {
        get_new_cleaner_segment(pmem);
    }

    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 22);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft get_emergency_cleaner_segment auf
static void test_6_get_emergency_seg(pmem_t *pmem, gconstpointer user_data) {
    get_emergency_cleaner_segment(pmem);

    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 22);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 0);
}

// ruft get_new_cleaner_segment öfter auf, als Segmente zur Verfügung sind
static void test_7_get_cleaner_seg_all_2(pmem_t *pmem, gconstpointer user_data) {
    for(int i=0; i<25; i++) {
        get_new_cleaner_segment(pmem);
    }

    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 23);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 0);
}

// ruft append_cleaner_segment_to_used_segments bei einem Cleaner Segment auf
static void test_8_append_cleaner_to_used(pmem_t *pmem, gconstpointer user_data) {
    get_new_cleaner_segment(pmem);

    append_cleaner_segment_to_used_segments(pmem, 1);
    
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 21);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft append_cleaner_segment_to_used_segments bei 3 Cleaner Segmenten auf
static void test_9_append_cleaner_to_used_2(pmem_t *pmem, gconstpointer user_data) {
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);

    append_cleaner_segment_to_used_segments(pmem, 2);
    
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 19);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 2);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft append_cleaner_segment_to_free_segments bei einem Cleaner Segment auf
static void test_10_append_cleaner_to_free(pmem_t *pmem, gconstpointer user_data) {
    get_new_cleaner_segment(pmem);

    append_cleaner_segment_to_free_segments(pmem, 1);
    
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 22);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft append_cleaner_segment_to_free_segments bei 3 Cleaner Segmenten auf
static void test_11_append_cleaner_to_free_2(pmem_t *pmem, gconstpointer user_data) {
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);

    append_cleaner_segment_to_free_segments(pmem, 2);
    
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 20);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 2);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft append_cleaned_segment_to_free_segments bei einem benutzten Segment auf
static void test_12_append_cleaned_to_free(pmem_t *pmem, gconstpointer user_data) {
    append_head_to_used_segments_and_get_new_head_segment(pmem);

    append_cleaned_segment_to_free_segments(pmem, 0);
    
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 22);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft append_cleaned_segment_to_free_segments bei 3 benutzten Segmenten auf
static void test_13_append_cleaned_to_free_2(pmem_t *pmem, gconstpointer user_data) {
    append_head_to_used_segments_and_get_new_head_segment(pmem);
    append_head_to_used_segments_and_get_new_head_segment(pmem);
    append_head_to_used_segments_and_get_new_head_segment(pmem);

    append_cleaned_segment_to_free_segments(pmem, 2);
    
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 20);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 2);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft append_all_cleaner_segments_to_used_segments bei 3 Cleaner Segmenten auf
static void test_14_append_all_cleaner_to_used(pmem_t *pmem, gconstpointer user_data) {
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);

    append_all_cleaner_segments_to_used_segments(pmem);
    
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 19);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 3);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft append_all_cleaner_segments_to_free_segments bei 3 Cleaner Segmenten auf
static void test_15_append_all_cleaner_to_free(pmem_t *pmem, gconstpointer user_data) {
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);

    append_all_cleaner_segments_to_free_segments(pmem);
    
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 22);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

// ruft append_all_cleaner_segments_to_free_segments bei 4 Cleaner Segmenten, inkl. Emergency Segment, auf
static void test_16_append_all_cleaner_to_free_2(pmem_t *pmem, gconstpointer user_data) {
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);
    get_new_cleaner_segment(pmem);
    get_emergency_cleaner_segment(pmem);

    append_all_cleaner_segments_to_free_segments(pmem);
    
    g_assert_cmpint(get_head_seg_count(pmem), ==, 1);
    g_assert_cmpint(get_free_seg_count(pmem), ==, 22);
    g_assert_cmpint(get_used_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_cleaner_seg_count(pmem), ==, 0);
    g_assert_cmpint(get_emergency_seg_count(pmem), ==, 1);
}

static void test_17_palloc_simple(pmem_t *pmem, gconstpointer user_data) {
    GHashTable *test_hash_table = g_hash_table_new_similar(pmem->hash_table);
     
    for(int i=0; i<20; i++) {
        size_t size = 0;
        while(!size) {
            size = rand() % (1U << 10); // 1024
        }

        char *str = malloc(size);
        if(!str) {
            fprintf(stderr, "malloc in test 17 failed\n");
            return;
        }

        random_string(str, size);
        uint64_t id = palloc(pmem, size, str);
        g_hash_table_insert(test_hash_table, GSIZE_TO_POINTER(id), str);
    }

    delete_pmem(pmem);
    init_pmem(pmem, "test_log_pmem");

    g_hash_table_foreach(test_hash_table, compare_data, pmem);

    g_hash_table_destroy(test_hash_table);
}

static void test_18_pfree_simple(pmem_t *pmem, gconstpointer user_data) {
    GHashTable *test_hash_table = g_hash_table_new_similar(pmem->hash_table);
    int to_free_count = 10;
    uint64_t to_free[to_free_count];
     
    for(int i=0; i<20; i++) {
        size_t size = 0;
        while(!size) {
            size = rand() % (1U << 10); // 1024
        }

        char *str = malloc(size);
        if(!str) {
            fprintf(stderr, "malloc in test 18 failed\n");
            return;
        }

        random_string(str, size);
        uint64_t id = palloc(pmem, size, str);
        g_hash_table_insert(test_hash_table, GSIZE_TO_POINTER(id), str);

        if(i<to_free_count) {
            to_free[i] = id;
        }
    }

    for(int i=0; i<to_free_count; i++) {
        pfree(pmem, to_free[i]);
    }

    delete_pmem(pmem);
    init_pmem(pmem, "test_log_pmem");

    for(int i=0; i<to_free_count; i++) {
        char *ret = get_address(pmem, to_free[i]);
        g_assert_null(ret);
        free(g_hash_table_lookup(test_hash_table, GSIZE_TO_POINTER(to_free[i])));
        g_hash_table_remove(test_hash_table, GSIZE_TO_POINTER(to_free[i]));
    }

    g_hash_table_foreach(test_hash_table, compare_data, pmem);

    g_hash_table_destroy(test_hash_table);
}

static void test_19_update_simple(pmem_t *pmem, gconstpointer user_data) {
    GHashTable *test_hash_table = g_hash_table_new_similar(pmem->hash_table);
    int to_update_count = 10;
    uint64_t to_update[to_update_count];
     
    for(int i=0; i<20; i++) {
        size_t size = 0;
        while(!size) {
            size = rand() % (1U << 10); // 1024
        }

        char *str = malloc(size);
        if(!str) {
            fprintf(stderr, "malloc in test 18 failed\n");
            return;
        }

        random_string(str, size);
        uint64_t id = palloc(pmem, size, str);
        g_hash_table_insert(test_hash_table, GSIZE_TO_POINTER(id), str);

        if(i<to_update_count) {
            to_update[i] = id;
            free(str);
        }
    }

    delete_pmem(pmem);
    init_pmem(pmem, "test_log_pmem");

    for(int i=0; i<to_update_count; i++) {
        size_t size = 0;
        while(!size) {
            size = rand() % (1U << 10); // 1024
        }

        char *str = malloc(size);
        if(!str) {
            fprintf(stderr, "malloc in test 18 failed\n");
            return;
        }

        random_string(str, size);
        update_data(pmem, to_update[i], str, size);
        g_hash_table_insert(test_hash_table, GSIZE_TO_POINTER(to_update[i]), str);
    }

    g_hash_table_foreach(test_hash_table, compare_data, pmem);

    g_hash_table_destroy(test_hash_table);
}

static void test_20_full(pmem_t *pmem, gconstpointer user_data) {
    GHashTable *test_hash_table = g_hash_table_new_similar(pmem->hash_table);
    int to_free_count = 0;
    int to_free_count_max = 20;
    uint64_t to_free[to_free_count_max];
    int to_update_count = 0;
    int to_update_count_max = 20;
    uint64_t to_update[to_update_count_max];
     
    for(int i=0; i<100; i++) {
        size_t size = 0;
        while(!size) {
            size = rand() % (1U << 10); // 1024
        }

        char *str = malloc(size);
        if(!str) {
            fprintf(stderr, "malloc in test 20 failed\n");
            return;
        }

        random_string(str, size);
        uint64_t id = palloc(pmem, size, str);
        g_hash_table_insert(test_hash_table, GSIZE_TO_POINTER(id), str);

        int r = rand();
        if(to_free_count<to_free_count_max && r % 4 == 0) {
            to_free[to_free_count++] = id;
        } else if(to_update_count<to_update_count_max && r % 4 == 1) {
            to_update[to_update_count++] = id;
        }
    }
    
    delete_pmem(pmem);
    init_pmem(pmem, "test_log_pmem");

    for(int i=0; i<to_free_count; i++) {
        pfree(pmem, to_free[i]);
        free(g_hash_table_lookup(test_hash_table, GSIZE_TO_POINTER(to_free[i])));
        g_hash_table_remove(test_hash_table, GSIZE_TO_POINTER(to_free[i]));
    }
    
    delete_pmem(pmem);
    init_pmem(pmem, "test_log_pmem");

    for(int i=0; i<to_update_count; i++) {
        size_t size = 0;
        while(!size) {
            size = rand() % (1U << 10); // 1024
        }

        char *str = malloc(size);
        if(!str) {
            fprintf(stderr, "malloc in test 20 failed\n");
            return;
        }

        free(g_hash_table_lookup(test_hash_table, GSIZE_TO_POINTER(to_update[i])));

        random_string(str, size);
        update_data(pmem, to_update[i], str, size);
        g_hash_table_insert(test_hash_table, GSIZE_TO_POINTER(to_update[i]), str);
    }

    g_hash_table_foreach(test_hash_table, compare_data, pmem);

    g_hash_table_destroy(test_hash_table);
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    g_test_init(&argc, &argv, NULL);
    // g_test_set_nonfatal_assertions();

    g_test_add("/log/init", pmem_t, NULL, setup, test_1_init, tear_down);
    g_test_add("/log/append_head", pmem_t, NULL, setup, test_2_append_head, tear_down);
    g_test_add("/log/append_head_all", pmem_t, NULL, setup, test_3_append_head_all, tear_down);
    
    g_test_add("/cleaner/get_cleaner_seg", pmem_t, NULL, setup, test_4_get_cleaner_seg, tear_down);
    g_test_add("/cleaner/get_cleaner_seg_all", pmem_t, NULL, setup, test_5_get_cleaner_seg_all, tear_down);
    g_test_add("/cleaner/get_emergency_seg", pmem_t, NULL, setup, test_6_get_emergency_seg, tear_down);
    g_test_add("/cleaner/get_cleaner_seg_all_2", pmem_t, NULL, setup, test_7_get_cleaner_seg_all_2, tear_down);

    g_test_add("/cleaner/append_cleaner_to_used", pmem_t, NULL, setup, test_8_append_cleaner_to_used, tear_down);
    g_test_add("/cleaner/append_cleaner_to_used_2", pmem_t, NULL, setup, test_9_append_cleaner_to_used_2, tear_down);
    
    g_test_add("/cleaner/append_cleaner_to_free", pmem_t, NULL, setup, test_10_append_cleaner_to_free, tear_down);
    g_test_add("/cleaner/append_cleaner_to_free_2", pmem_t, NULL, setup, test_11_append_cleaner_to_free_2, tear_down);

    g_test_add("/cleaner/append_cleaned_to_free_", pmem_t, NULL, setup, test_12_append_cleaned_to_free, tear_down);
    g_test_add("/cleaner/append_cleaned_to_free_2", pmem_t, NULL, setup, test_13_append_cleaned_to_free_2, tear_down);

    g_test_add("/cleaner/append_all_cleaner_to_used", pmem_t, NULL, setup, test_14_append_all_cleaner_to_used, tear_down);

    g_test_add("/cleaner/append_all_cleaner_to_free", pmem_t, NULL, setup, test_15_append_all_cleaner_to_free, tear_down);
    g_test_add("/cleaner/append_all_cleaner_to_free_2", pmem_t, NULL, setup, test_16_append_all_cleaner_to_free_2, tear_down);

    g_test_add("/log_pmem/palloc_simple", pmem_t, NULL, setup, test_17_palloc_simple, tear_down);
    g_test_add("/log_pmem/pfree_simple", pmem_t, NULL, setup, test_18_pfree_simple, tear_down);
    g_test_add("/log_pmem/update_simple", pmem_t, NULL, setup, test_19_update_simple, tear_down);

    g_test_add("/log_pmem/full", pmem_t, NULL, setup, test_20_full, tear_down);

    return g_test_run();
}
