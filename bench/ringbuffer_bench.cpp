#include <chrono>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include "events.h"

using namespace polypnl;

template<typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
public:
    SPSCRingBuffer() : buffer_(new T[Capacity]()) {}
    ~SPSCRingBuffer() { delete[] buffer_; }

    bool push(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) == Capacity) return false;
        buffer_[head & (Capacity - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        item = buffer_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    T* buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

int main() {
    constexpr size_t N = 5'000'000;
    std::cout << "Benchmarking SPSC lock-free ring buffer (" << N << " elements across threads)..." << std::endl;

    SPSCRingBuffer<NormalizedEvent, 65536> ring;
    NormalizedEvent sample{};
    sample.qty = 1000;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&]() {
        for (size_t i = 0; i < N; ++i) {
            while (!ring.push(sample)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        NormalizedEvent item{};
        for (size_t i = 0; i < N; ++i) {
            while (!ring.pop(item)) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    double ops_per_sec = (1e9 * N) / elapsed_ns;
    std::cout << "Elapsed: " << (elapsed_ns / 1e6) << " ms\n"
              << "SPSC throughput: " << static_cast<uint64_t>(ops_per_sec) << " msg/sec\n";

    return 0;
}
