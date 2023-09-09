extern "C" {
    #include "../../../../project/log_pmem.h"
}

#include <inttypes.h>

namespace ycsbc {
    class LogPmem : public StoredsBase {
    public:
        LogPmem(const char *path) {
            LogPmem::init(path);
        }

        int init(const char *path);

        int read(const uint64_t key, void *&result);

        int scan(const uint64_t key, int len, std::vector <std::vector<DB::Kuint64VstrPair>> &result);

        int update(const uint64_t key, void *value);

        int insert(const uint64_t key, void *value);

        void destroy();

        ~LogPmem() {
            LogPmem::destroy();
        }

    private:
        pmem_t *pmem;
    };

    int LogPmem::init(const char *path) {
        pmem = (pmem_t*)malloc(sizeof(pmem_t));
        if(!pmem) {
            fprintf(stderr, "malloc failed");
            exit(EXIT_FAILURE);
        }
        if(init_pmem(pmem, (char*)path)) {
            free(pmem);
            exit(EXIT_FAILURE);
        }
        return 0;
    }

    int LogPmem::read(const uint64_t key, void *&result) {
        result = get_address(pmem, key);
        return result ? 0 : 1;
    }

    int LogPmem::scan(const uint64_t key, int len, std::vector <std::vector<DB::Kuint64VstrPair>> &result) {
        return 0;
    }

    int LogPmem::update(const uint64_t key, void *value) {
        return update_data(pmem, key, value, strlen((char*) value) + 1);
    }

    int LogPmem::insert(const uint64_t key, void *value) {
        return palloc_with_id(pmem, key, strlen((char*) value) + 1, value);
    }

    void LogPmem::destroy() {
        delete_pmem(pmem);
        return;
    }
}
