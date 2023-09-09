#include <glib.h>
#include "log_pmem.h"
#include "log.h"
#include "cleaner.h"
#include "hash_table.h"


static void setup(pmem_t *pmem, gconstpointer user_data) {
    size_t size = *(size_t*)user_data;
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

    if(ftruncate(fd, size)) {
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

static void test_1_full(pmem_t *pmem, gconstpointer user_data) {
    GHashTable *test_hash_table = g_hash_table_new(g_direct_hash, g_direct_equal);
    int to_free_count = 0;
    int to_free_count_max = 400;
    uint64_t to_free[to_free_count_max];
    int to_update_count = 0;
    int to_update_count_max = 400;
    uint64_t to_update[to_update_count_max];
     
    for(int i=0; i<1200; i++) {
        size_t size = 0;
        while(!size) {
            size = rand() % (1U << 10); // 1024
        }

        char *str = malloc(size);
        if(!str) {
            fprintf(stderr, "malloc in test 1 failed\n");
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
            fprintf(stderr, "malloc in test 1 failed\n");
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

static void test_2_full(pmem_t *pmem, gconstpointer user_data) {
    GHashTable *test_hash_table = g_hash_table_new(g_direct_hash, g_direct_equal);
    int to_free_count = 0;
    int to_free_count_max = 800;
    uint64_t to_free[to_free_count_max];
    int to_update_count = 0;
    int to_update_count_max = 800;
    uint64_t to_update[to_update_count_max];
     
    for(int i=0; i<2500; i++) {
        size_t size = 0;
        while(!size) {
            size = rand() % (1U << 10); // 1024
        }

        char *str = malloc(size);
        if(!str) {
            fprintf(stderr, "malloc in test 2 failed\n");
            return;
        }

        random_string(str, size);
        uint64_t id = palloc(pmem, size, str);
        if(!id) {
            continue;
        }
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
            fprintf(stderr, "malloc in test 2 failed\n");
            return;
        }

        random_string(str, size);
        int ret = update_data(pmem, to_update[i], str, size);
        if(ret) {
            continue;
        }
        free(g_hash_table_lookup(test_hash_table, GSIZE_TO_POINTER(to_update[i])));
        g_hash_table_insert(test_hash_table, GSIZE_TO_POINTER(to_update[i]), str);
    }

    for(int i=0; i<500; i++) {
        size_t size = 0;
        while(!size) {
            size = rand() % (1U << 10); // 1024
        }

        char *str = malloc(size);
        if(!str) {
            fprintf(stderr, "malloc in test 2 failed\n");
            return;
        }

        random_string(str, size);
        uint64_t id = palloc(pmem, size, str);
        if(!id) {
            continue;
        }
        g_hash_table_insert(test_hash_table, GSIZE_TO_POINTER(id), str);
    }

    g_hash_table_foreach(test_hash_table, compare_data, pmem);

    g_hash_table_destroy(test_hash_table);
}


int main(int argc, char *argv[]) {
    srand(time(NULL));
    g_test_init(&argc, &argv, NULL);
    // g_test_set_nonfatal_assertions();
    size_t size1 = (1U << 20); // 1 MiB
    size_t size2 = (1U << 21); // 2 MiB

    g_test_add("/log_pmem/full_big", pmem_t, &size1, setup, test_1_full, tear_down);
    g_test_add("/log_pmem/full_big_2", pmem_t, &size2, setup, test_2_full, tear_down);

    return g_test_run();
}
