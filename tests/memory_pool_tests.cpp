#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <set>
#include <stdexcept>
#include "memory_pool.h"

struct Trivial {
    int x;
    int y;
};

struct NonTrivial {
    std::string name;
    int value;

    NonTrivial(std::string n, int v) : name(std::move(n)), value(v) {}
};

// Tracks constructor/destructor calls
struct Tracked {
    inline static int constructor_calls = 0;
    inline static int destructor_calls  = 0;

    Tracked()  { ++constructor_calls; }
    ~Tracked() { ++destructor_calls;  }

    static void reset() { constructor_calls = destructor_calls = 0; }
};

// Basic allocation

TEST(MemoryPoolTest, AllocateReturnsNonNull) {
    MemoryPool<Trivial, 8> pool;
    Trivial* p = pool.allocate();
    ASSERT_NE(p, nullptr);
}

TEST(MemoryPoolTest, AllocateReturnsUniquePointers) {
    MemoryPool<Trivial, 8> pool;
    std::set<Trivial*> ptrs;
    for (int i = 0; i < 8; ++i) {
        Trivial* p = pool.allocate();
        ASSERT_NE(p, nullptr);
        EXPECT_TRUE(ptrs.insert(p).second) << "Duplicate pointer returned at index " << i;
    }
}

TEST(MemoryPoolTest, AllocatedMemoryIsWritable) {
    MemoryPool<Trivial, 4> pool;
    Trivial* p = pool.allocate();
    new(p) Trivial{42, 99};
    EXPECT_EQ(p->x, 42);
    EXPECT_EQ(p->y, 99);
}

TEST(MemoryPoolTest, AllocateFullPoolReturnsNullptr) {
    MemoryPool<Trivial, 4> pool;
    for (int i = 0; i < 4; ++i)
        pool.allocate();
    EXPECT_EQ(pool.allocate(), nullptr);
}

TEST(MemoryPoolTest, AllocateFromEmptyPoolAfterDeallocate) {
    MemoryPool<Trivial, 1> pool;
    Trivial* p = pool.allocate();
    ASSERT_NE(p, nullptr);
    pool.deallocate(p);
    Trivial* p2 = pool.allocate();
    EXPECT_NE(p2, nullptr);
}

// Capacity and sizing

TEST(MemoryPoolTest, ExactCapacityAllocationsSucceed) {
    constexpr size_t N = 16;
    MemoryPool<Trivial, N> pool;
    std::vector<Trivial*> ptrs;
    for (size_t i = 0; i < N; ++i) {
        Trivial* p = pool.allocate();
        ASSERT_NE(p, nullptr) << "Allocation " << i << " failed unexpectedly";
        ptrs.push_back(p);
    }
}

TEST(MemoryPoolTest, OneOverCapacityFails) {
    constexpr size_t N = 4;
    MemoryPool<Trivial, N> pool;
    for (size_t i = 0; i < N; ++i)
        pool.allocate();
    EXPECT_EQ(pool.allocate(), nullptr);
}

// ---------------------------------------------------------------------------
// Deallocation and reuse
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, DeallocateAllowsReuse) {
    MemoryPool<Trivial, 4> pool;
    std::vector<Trivial*> ptrs;
    for (int i = 0; i < 4; ++i)
        ptrs.push_back(pool.allocate());

    // free all
    for (auto p : ptrs)
        pool.deallocate(p);

    // should be able to allocate again
    for (int i = 0; i < 4; ++i)
        EXPECT_NE(pool.allocate(), nullptr);
}

TEST(MemoryPoolTest, PartialDeallocateAllowsExactReuse) {
    MemoryPool<Trivial, 4> pool;
    Trivial* p0 = pool.allocate();
    Trivial* p1 = pool.allocate();
    pool.allocate();
    pool.allocate();

    // pool full — free two
    pool.deallocate(p0);
    pool.deallocate(p1);

    // exactly two slots should be available
    EXPECT_NE(pool.allocate(), nullptr);
    EXPECT_NE(pool.allocate(), nullptr);
    EXPECT_EQ(pool.allocate(), nullptr);  // still full
}

TEST(MemoryPoolTest, ReallocatedPointerIsUsable) {
    MemoryPool<Trivial, 2> pool;
    Trivial* p = pool.allocate();
    new(p) Trivial{1, 2};
    pool.deallocate(p);

    Trivial* p2 = pool.allocate();
    ASSERT_NE(p2, nullptr);
    new(p2) Trivial{7, 8};
    EXPECT_EQ(p2->x, 7);
    EXPECT_EQ(p2->y, 8);
}

// Double-free detection

TEST(MemoryPoolTest, DoubleFreeThrows) {
    MemoryPool<Trivial, 4> pool;
    Trivial* p = pool.allocate();
    pool.deallocate(p);
    EXPECT_THROW(pool.deallocate(p), std::runtime_error);
}


TEST(MemoryPoolTest, DoubleFreeMessageIsDescriptive) {
    MemoryPool<Trivial, 4> pool;
    Trivial* p = pool.allocate();
    pool.deallocate(p);
    try {
        pool.deallocate(p);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("Double"), std::string::npos)
            << "Exception message should mention 'Double': " << e.what();
    }
}

// Invalid pointer detection

TEST(MemoryPoolTest, DeallocateNullptrSucceeds) {
    MemoryPool<Trivial, 4> pool;
    EXPECT_NO_THROW(pool.deallocate(nullptr));
}

TEST(MemoryPoolTest, DeallocateOutOfRangePointerThrows) {
    MemoryPool<Trivial, 4> pool;
    Trivial external{};
    EXPECT_THROW(pool.deallocate(&external), std::runtime_error);
}

// Alignment

TEST(MemoryPoolTest, AllocatedPointersAreAligned) {
    MemoryPool<Trivial, 8> pool;
    for (int i = 0; i < 8; ++i) {
        Trivial* p = pool.allocate();
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignof(Trivial), 0)
            << "Pointer " << p << " is not aligned to " << alignof(Trivial);
    }
}

TEST(MemoryPoolTest, CacheLineAlignedTypeIsAligned) {
    struct alignas(64) CacheAligned { char data[64]; };
    MemoryPool<CacheAligned, 4> pool;
    for (int i = 0; i < 4; ++i) {
        CacheAligned* p = pool.allocate();
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 64, 0)
            << "Cache-aligned type not on 64-byte boundary";
    }
}

// Non-trivial types

TEST(MemoryPoolTest, NonTrivialTypeIsStoredCorrectly) {
    MemoryPool<NonTrivial, 4> pool;
    NonTrivial* p = pool.allocate();
    ASSERT_NE(p, nullptr);
    new(p) NonTrivial{"hello", 42};
    EXPECT_EQ(p->name, "hello");
    EXPECT_EQ(p->value, 42);
    p->~NonTrivial();
    pool.deallocate(p);
}

// Since the pool gives you raw memory,  the caller is responsible (placement new / manual dtor call).
// These tests verify the pool doesn't do anything with the actual data.

TEST(MemoryPoolTest, PoolDoesNotCorruptStoredData) {
    MemoryPool<Trivial, 4> pool;
    Trivial* a = pool.allocate();
    Trivial* b = pool.allocate();
    new(a) Trivial{111, 222};
    new(b) Trivial{333, 444};

    // allocate/deallocate something else — should not disturb a and b
    Trivial* c = pool.allocate();
    pool.deallocate(c);
    pool.allocate();

    EXPECT_EQ(a->x, 111);
    EXPECT_EQ(a->y, 222);
    EXPECT_EQ(b->x, 333);
    EXPECT_EQ(b->y, 444);
}

// Repeated alloc/dealloc cycles

TEST(MemoryPoolTest, RepeatedAllocDeallocCycles) {
    MemoryPool<Trivial, 4> pool;
    for (int cycle = 0; cycle < 1000; ++cycle) {
        std::vector<Trivial*> ptrs;
        for (int i = 0; i < 4; ++i) {
            Trivial* p = pool.allocate();
            ASSERT_NE(p, nullptr) << "Failed on cycle " << cycle << " alloc " << i;
            ptrs.push_back(p);
        }
        EXPECT_EQ(pool.allocate(), nullptr);
        for (auto p : ptrs)
            pool.deallocate(p);
    }
}

TEST(MemoryPoolTest, InterleavedAllocDealloc) {
    MemoryPool<Trivial, 4> pool;
    Trivial* p0 = pool.allocate();
    Trivial* p1 = pool.allocate();
    pool.deallocate(p0);
    Trivial* p2 = pool.allocate();
    pool.deallocate(p1);
    Trivial* p3 = pool.allocate();
    pool.deallocate(p2);
    pool.deallocate(p3);
    // all 4 slots free again
    for (int i = 0; i < 4; ++i)
        EXPECT_NE(pool.allocate(), nullptr);
}