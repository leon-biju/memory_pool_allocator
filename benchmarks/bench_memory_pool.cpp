#include <benchmark/benchmark.h>
#include <vector>
#include <random>
#include "memory_pool.h"

struct Small {
    int x, y;
};

struct Medium {
    double a, b, c, d;
    int id;
    char name[32];
};

struct Large {
    char data[256];
    int id;
};

struct alignas(64) CacheAligned {
    char data[64];
};


// Helper function to produce a shuffled index sequence for random-order deallocation benchmarks
// to emulate realistic usage
static std::vector<size_t> shuffled_indices(const size_t n) {
    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    std::mt19937 rng(67);
    std::ranges::shuffle(indices, rng);
    return indices;
}

// Sequential alloc then sequential dealloc
// allocate N ptrs, then free N ptrs in same order
template <typename T>
static void BM_NewDelete_Sequential(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<T*> ptrs(N);
    for (auto _ : state) {
        for (size_t i = 0; i < N; ++i)
            ptrs[i] = new T;
        for (size_t i = 0; i < N; ++i)
            delete ptrs[i];
    }
    state.SetItemsProcessed(state.iterations() * N * 2);
}

template <typename T, size_t N>
static void BM_Pool_Sequential(benchmark::State& state) {
    MemoryPool<T, N> pool;
    std::vector<T*> ptrs(N);
    for (auto _ : state) {
        for (size_t i = 0; i < N; ++i)
            ptrs[i] = pool.allocate();
        for (size_t i = 0; i < N; ++i)
            pool.deallocate(ptrs[i]);
    }
    state.SetItemsProcessed(state.iterations() * N * 2);
}

BENCHMARK(BM_NewDelete_Sequential<Small>)->Arg(64)->Arg(256)->Arg(1024);
BENCHMARK(BM_Pool_Sequential<Small,    1024>)->Arg(64)->Arg(256)->Arg(1024);

BENCHMARK(BM_NewDelete_Sequential<Medium>)->Arg(64)->Arg(256)->Arg(1024);
BENCHMARK(BM_Pool_Sequential<Medium,   1024>)->Arg(64)->Arg(256)->Arg(1024);

BENCHMARK(BM_NewDelete_Sequential<Large>)->Arg(64)->Arg(256)->Arg(1024);
BENCHMARK(BM_Pool_Sequential<Large,    1024>)->Arg(64)->Arg(256)->Arg(1024);



// Random-order deallocation
// More realistic objects are freed in a different order than they were allocated.
// this is where the memory pool mogs because of new/delete having fragmentation
template <typename T>
static void BM_NewDelete_RandomFree(benchmark::State& state) {
    const size_t N = state.range(0);
    auto order = shuffled_indices(N);
    std::vector<T*> ptrs(N);
    for (auto _ : state) {
        for (size_t i = 0; i < N; ++i)
            ptrs[i] = new T;
        for (size_t i = 0; i < N; ++i)
            delete ptrs[order[i]];
    }
    state.SetItemsProcessed(state.iterations() * N * 2);
}

template <typename T, size_t N>
static void BM_Pool_RandomFree(benchmark::State& state) {
    MemoryPool<T, N> pool;
    auto order = shuffled_indices(N);
    std::vector<T*> ptrs(N);
    for (auto _ : state) {
        for (size_t i = 0; i < N; ++i)
            ptrs[i] = pool.allocate();
        for (size_t i = 0; i < N; ++i)
            pool.deallocate(ptrs[order[i]]);
    }
    state.SetItemsProcessed(state.iterations() * N * 2);
}

BENCHMARK(BM_NewDelete_RandomFree<Small>)->Arg(64)->Arg(256)->Arg(1024);
BENCHMARK(BM_Pool_RandomFree<Small,    1024>)->Arg(64)->Arg(256)->Arg(1024);

BENCHMARK(BM_NewDelete_RandomFree<Medium>)->Arg(64)->Arg(256)->Arg(1024);
BENCHMARK(BM_Pool_RandomFree<Medium,   1024>)->Arg(64)->Arg(256)->Arg(1024);

// Single allocate + deallocate in a tight loop
// Measures raw per-operation cost with no batching
static void BM_NewDelete_SingleOp(benchmark::State& state) {
    for (auto _ : state) {
        Small* p = new Small;
        benchmark::DoNotOptimize(p);
        delete p;
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_Pool_SingleOp(benchmark::State& state) {
    MemoryPool<Small, 1> pool;
    for (auto _ : state) {
        Small* p = pool.allocate();
        benchmark::DoNotOptimize(p);
        pool.deallocate(p);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_NewDelete_SingleOp);
BENCHMARK(BM_Pool_SingleOp);

// Interleaved alloc/dealloc
// Allocate half the pool, then repeatedly free one and allocate one.
// Simulates a steady-state system where the pool is partially full.
template <typename T>
static void BM_NewDelete_Interleaved(benchmark::State& state) {
    std::vector<T*> ptrs;
    ptrs.reserve(512);
    for (size_t i = 0; i < 256; ++i)
        ptrs.push_back(new T);

    size_t head = 0;
    for (auto _ : state) {
        delete ptrs[head % 256];
        T* p = new T;
        benchmark::DoNotOptimize(p);
        ptrs[head % 256] = p;
        ++head;
    }
    state.SetItemsProcessed(state.iterations());

    for (auto p : ptrs) delete p;
}

template <typename T, size_t N>
static void BM_Pool_Interleaved(benchmark::State& state) {
    MemoryPool<T, N> pool;
    std::vector<T*> ptrs;
    ptrs.reserve(N);

    // fill half the pool
    for (size_t i = 0; i < N / 2; ++i)
        ptrs.push_back(pool.allocate());

    size_t head = 0;
    for (auto _ : state) {
        pool.deallocate(ptrs[head % (N / 2)]);
        T* p = pool.allocate();
        benchmark::DoNotOptimize(p);
        ptrs[head % (N / 2)] = p;
        ++head;
    }
    state.SetItemsProcessed(state.iterations());
}


BENCHMARK(BM_NewDelete_Interleaved<Small>);
BENCHMARK(BM_Pool_Interleaved<Small, 512>);

BENCHMARK(BM_NewDelete_Interleaved<Medium>);
BENCHMARK(BM_Pool_Interleaved<Medium, 512>);

// Cache behaviour test
// Measures how much faster it is to iterate over pool-allocated objects
// vs heap-allocated objects due to memory locality.
// once again common pool W

static void BM_NewDelete_SequentialAccess(benchmark::State& state) {
    constexpr size_t N = 1024;
    std::vector<Medium*> ptrs(N);
    for (size_t i = 0; i < N; ++i) {
        ptrs[i] = new Medium;
        ptrs[i]->id = static_cast<int>(i);
    }

    long sum = 0;
    for (auto _ : state) {
        for (size_t i = 0; i < N; ++i)
            sum += ptrs[i]->id;
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * N);

    for (auto p : ptrs) delete p;
}

static void BM_Pool_SequentialAccess(benchmark::State& state) {
    constexpr size_t N = 1024;
    MemoryPool<Medium, 1024> pool;
    std::vector<Medium*> ptrs(N);
    for (size_t i = 0; i < N; ++i) {
        ptrs[i] = pool.allocate();
        ptrs[i]->id = static_cast<int>(i);
    }

    long sum = 0;
    for (auto _ : state) {
        for (size_t i = 0; i < N; ++i)
            sum += ptrs[i]->id;
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * N);
}

BENCHMARK(BM_NewDelete_SequentialAccess);
BENCHMARK(BM_Pool_SequentialAccess);

// Cache-aligned type
// Verifies that alignas(64) types are handled correctly and
// that the pool still outperforms new/delete for aligned allocations

static void BM_NewDelete_CacheAligned(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<CacheAligned*> ptrs(N);
    for (auto _ : state) {
        for (size_t i = 0; i < N; ++i)
            ptrs[i] = new CacheAligned;
        for (size_t i = 0; i < N; ++i)
            delete ptrs[i];
    }
    state.SetItemsProcessed(state.iterations() * N * 2);
}

static void BM_Pool_CacheAligned(benchmark::State& state) {
    const size_t N = state.range(0);
    MemoryPool<CacheAligned, 256> pool;
    std::vector<CacheAligned*> ptrs(N);
    for (auto _ : state) {
        for (size_t i = 0; i < N; ++i)
            ptrs[i] = pool.allocate();
        for (size_t i = 0; i < N; ++i)
            pool.deallocate(ptrs[i]);
    }
    state.SetItemsProcessed(state.iterations() * N * 2);
}

BENCHMARK(BM_NewDelete_CacheAligned)->Arg(64)->Arg(256);
BENCHMARK(BM_Pool_CacheAligned)->Arg(64)->Arg(256);




BENCHMARK_MAIN();