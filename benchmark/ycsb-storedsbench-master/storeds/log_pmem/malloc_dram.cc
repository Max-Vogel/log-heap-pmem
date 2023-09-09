#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unordered_map>

namespace ycsbc {
    class MallocDram : public StoredsBase {
    public:
        MallocDram(const char *path) {
            MallocDram::init(path);
        }

        int init(const char *path);

        int read(const uint64_t key, void *&result);

        int scan(const uint64_t key, int len, std::vector <std::vector<DB::Kuint64VstrPair>> &result);

        int update(const uint64_t key, void *value);

        int insert(const uint64_t key, void *value);

        void destroy();

        ~MallocDram() {
            MallocDram::destroy();
        }

    private:
        // std::unordered_map<uint64_t, void*> addresses;
        char *data;
    };

    int MallocDram::init(const char *path) {
        data = (char*)malloc((1U << 11));
        return 0;
    }

    int MallocDram::read(const uint64_t key, void *&result) {
        // result = addresses.at(key);
        result = data;
        return 0;
    }

    int MallocDram::scan(const uint64_t key, int len, std::vector <std::vector<DB::Kuint64VstrPair>> &result) {
        return 0;
    }

    int MallocDram::update(const uint64_t key, void *value) {
        // void *data = addresses.at(key);
        // MallocDram::insert(key, value);
        // free(data);
        strcpy(data, (char*)value);
        return 0;
    }

    int MallocDram::insert(const uint64_t key, void *value) {
        char *str =(char*)malloc(strlen((char*)value) + 1);
        strcpy(data, (char*)value);
        free(str);
        return 0;
    }

    void MallocDram::destroy() {
        free(data);
        // for (auto& x: addresses) {
        //     free(x.second);
        // }
        return;
    }
}
