#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <libpmemobj.h>
#include "../../lib/ex_common.h"

// #define PMEM_LL_POOL_SIZE ((size_t) (1 << 30))
// // #define LL_LAYOUT_NAME "pmemobj_pmem_layout"
// #define CREATE_MODE_RW (S_IWUSR | S_IRUSR)

typedef struct data {
    char data[(1<<11)];
} data_t;

typedef struct root {
    PMEMoid data;
} root_t;

namespace ycsbc {
    class PmemobjPmem : public StoredsBase {
    public:
        PmemobjPmem(const char *path) {
            PmemobjPmem::init(path);
        }

        int init(const char *path);

        int read(const uint64_t key, void *&result);

        int scan(const uint64_t key, int len, std::vector <std::vector<DB::Kuint64VstrPair>> &result);

        int update(const uint64_t key, void *value);

        int insert(const uint64_t key, void *value);

        void destroy();

        ~PmemobjPmem() {
            PmemobjPmem::destroy();
        }

    private:
        PMEMobjpool *pop;
        root_t *rootp;
        // TOID(root_t) root;
    };

    int PmemobjPmem::init(const char *path) {
        // pop = pmemobj_open(path, "test");
        // if (pop == NULL) {
        //     perror("pmemobj_open");
        //     return 1;
        // }

        if (file_exists(path) != 0) {
            if ((pop = pmemobj_create(path, LL_LAYOUT_NAME, PMEM_LL_POOL_SIZE, CREATE_MODE_RW)) == NULL) {
                fprintf(stderr, "failed to create pool: %s\n", pmemobj_errormsg());
                return 1;
            }
        } else {
            if ((pop = pmemobj_open(path, LL_LAYOUT_NAME)) == NULL) {
                fprintf(stderr, "failed to open pool: %s\n", pmemobj_errormsg());
                return 1;
            }
        }

        PMEMoid root = pmemobj_root(pop, sizeof(root_t));
        rootp = (root_t*)pmemobj_direct(root);

        PMEMoid new_data;
        pmemobj_alloc(pop, &new_data, sizeof(data_t), 0, NULL, NULL);
        rootp->data = new_data;
        pmemobj_persist(pop, rootp, sizeof(root_t));
        
        return 0;
    }

    int PmemobjPmem::read(const uint64_t key, void *&result) {
        data_t *data = (data_t*) pmemobj_direct(rootp->data);
        result = data->data;
        return 0;
    }

    int PmemobjPmem::scan(const uint64_t key, int len, std::vector <std::vector<DB::Kuint64VstrPair>> &result) {
        return 0;
    }

    int PmemobjPmem::update(const uint64_t key, void *value) {
        data_t *data = (data_t*) pmemobj_direct(rootp->data);
        pmemobj_memcpy_persist(pop, data->data, (char *) value, strlen((char *) value) + 1);
        return 0;
    }

    int PmemobjPmem::insert(const uint64_t key, void *value) {   
        PMEMoid new_data;
        pmemobj_alloc(pop, &new_data, sizeof(data_t), 0, NULL, NULL);
        data_t *new_data_ptr = (data_t*) pmemobj_direct(new_data);
        pmemobj_memcpy_persist(pop, new_data_ptr->data, (char *) value, strlen((char *) value) + 1);
        pmemobj_free(&rootp->data);
        rootp->data = new_data;
        pmemobj_persist(pop, rootp, sizeof(root_t));
        return 0;
    }

    void PmemobjPmem::destroy() {
        pmemobj_free(&rootp->data);
        pmemobj_close(pop);
        return;
    }
}
