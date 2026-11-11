#include <chrono>
#include <iostream>
#include <vector>
#include "engine.h"

using namespace polypnl;
using namespace polypnl::engine;

int main() {
    PositionMap map;

    constexpr int N = 1'000'000;
    std::cout << "Benchmarking PositionMap open-addressing hash table (" << N << " inserts/lookups)..." << std::endl;

    auto t1 = std::chrono::high_resolution_clock::now();
    for (uint32_t i = 1; i <= N; ++i) {
        PositionKey k{i, (i % 8) + 1};
        Position* p = map.get_or_create(k);
        if (p) {
            p->size += 1000;
        }
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    auto insert_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
    std::cout << "Insert throughput: " << static_cast<uint64_t>(1e9 * N / insert_ns) << " ops/sec ("
              << (insert_ns / static_cast<double>(N)) << " ns/op)\n";

    auto t3 = std::chrono::high_resolution_clock::now();
    uint64_t sum = 0;
    for (uint32_t i = 1; i <= N; ++i) {
        PositionKey k{i, (i % 8) + 1};
        Position* p = map.find(k);
        if (p) sum += p->size;
    }
    auto t4 = std::chrono::high_resolution_clock::now();

    auto lookup_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t4 - t3).count();
    std::cout << "Lookup throughput: " << static_cast<uint64_t>(1e9 * N / lookup_ns) << " ops/sec ("
              << (lookup_ns / static_cast<double>(N)) << " ns/op)\n";

    (void)sum;
    return 0;
}
