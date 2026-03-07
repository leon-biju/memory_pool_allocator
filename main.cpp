#include <print>
#include "memory_pool.h"

int main() {
    MemoryPool<std::string, 64> Pool;

    std::string* k = Pool.allocate();
    *k = "hello world";
    std::println(stderr, "{}", *k);
    Pool.deallocate(k);

    //try and double delete
    Pool.deallocate(k);

    return 0;
}