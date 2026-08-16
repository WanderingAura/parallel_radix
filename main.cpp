#include <barrier>
#include <iostream>
#include <cstdint>
#include <vector>
#include <array>
#include <cstring>
#include <thread>
#include <cassert>
#include <functional>

#ifndef NDEBUG
#define RADDBG_MARKUP_IMPLEMENTATION
#else
#define RADDBUG_MARKUP_STUBS
#endif
#include "raddbg_markup.h"

typedef uint64_t u64;
typedef uint8_t u8;
typedef uint8_t u16;
typedef uint32_t u32;

struct ThreadContext
{
    u32 tidx;
    u32 tcount;

    u8 *shared_memory;
};

struct Range
{
    u64 start;
    u64 end;
};

// divides N into chunk_count equally distributed chunks and returns the range which the chunk_idx-th chunk occupies
Range ChunkNRangeFromCount(u64 chunk_idx, u64 chunk_count, u64 total_size)
{
    u64 main_chunk_size = total_size / chunk_count;
    u64 remainder = total_size % chunk_count;

    // distribute the remainder among the first `remainder` chunks
    u64 start = main_chunk_size * chunk_idx;
    u64 end = main_chunk_size * (chunk_idx+1);
    start += std::min(remainder, chunk_idx);
    end += std::min(remainder, chunk_idx+1);

    return Range{start,end};
}

// TODO: consider whether this should be a singleton. this class likely breaks if there are multiple instances
class ParallelAlgorithm
{
public:
    ParallelAlgorithm(u32 worker_count = std::thread::hardware_concurrency()) :
        workers_(worker_count)
    {
        tctx_ = {};
    }

    template<typename Callable, typename... Args>
    static void ThreadEntry(ThreadContext ctx, Callable&& algorithm, Args&& ...args)
    {
        raddbg_thread_id(ctx.tidx);
        char thread_id_str[4] = {};
        // assumes thread idx never goes above 99
        assert(ctx.tidx < 100);
        thread_id_str[0] = '0' + ctx.tidx / 10;
        thread_id_str[1] = '0' + ctx.tidx % 10;
        raddbg_thread_name(thread_id_str);

        tctx_ = ctx;

        std::invoke(std::forward<Callable>(algorithm), std::forward<Args>(args)...);
    }

    template<typename Callable, typename... Args>
    void Execute(Callable&& algorithm, Args&& ...args)
    {
        thread_sync_ = new std::barrier<>(workers_.size());

        for (u32 i = 0; i < workers_.size(); i++)
        {
            ThreadContext ctx{.tidx = static_cast<u16>(i), .tcount = static_cast<u16>(workers_.size())};
            workers_[i] = std::thread([ctx, &algorithm, &args...]() {
                ThreadEntry(ctx, algorithm, args...);
            });
        }

        for (auto& worker : workers_)
        {
            worker.join();
        }

        delete thread_sync_;
    }

    static u32 LaneIdx()
    {
        return tctx_.tidx;
    }

    static u32 LaneCount()
    {
        return tctx_.tcount;
    }

    static void LaneSync()
    {
        thread_sync_->arrive_and_wait();
    }

    template<typename T>
    static void LaneSyncBroadcast(T* p, u32 src_lane)
    {
        assert(sizeof(T) <= sizeof(broadcast_memory_));

        if (LaneIdx() == src_lane)
        {
            ::memcpy(broadcast_memory_, p, sizeof(T));
        }

        LaneSync();

        ::memcpy(p, broadcast_memory_, sizeof(T));

        LaneSync();
    }

    static Range ChunkForLane(u64 total_size)
    {
        return ChunkNRangeFromCount(LaneIdx(), LaneCount(), total_size);
    }

private:
    static thread_local ThreadContext tctx_;
    static std::barrier<>* thread_sync_;
    std::vector<std::thread> workers_;

    alignas(std::max_align_t) static u8 broadcast_memory_[64];
};

thread_local ThreadContext ParallelAlgorithm::tctx_ = {};
std::barrier<>* ParallelAlgorithm::thread_sync_ = {};
alignas(std::max_align_t) u8 ParallelAlgorithm::broadcast_memory_[64] = {};

void ParallelRadixSort(std::vector<u64>& arr)
{
    u32 lane_count = ParallelAlgorithm::LaneCount();
    u32 lane_idx = ParallelAlgorithm::LaneIdx();
    u64 n = arr.size();

    Range chunk = ParallelAlgorithm::ChunkForLane(n);
    u64 lane_start = chunk.start;
    u64 lane_end = chunk.end;

    // 2d array with dimensions (lane, digit) = (thread_count, 256); recomputed fresh each
    // byte pass below, since each pass reorders arr and a lane's fixed index range ends up
    // holding a different set of values than it started with.
    using LaneDigitArray = std::vector<std::array<u32,256>>;
    LaneDigitArray* pcounts = nullptr;
    LaneDigitArray* poffsets = nullptr;
    std::vector<u64>* pback_buffer = nullptr;
    if (lane_idx == 0)
    {
        pcounts = new LaneDigitArray(lane_count);
        poffsets = new LaneDigitArray(lane_count);
        pback_buffer = new std::vector<u64>(arr.size());
    }

    ParallelAlgorithm::LaneSyncBroadcast(&pcounts, 0);
    ParallelAlgorithm::LaneSyncBroadcast(&poffsets, 0);
    ParallelAlgorithm::LaneSyncBroadcast(&pback_buffer, 0);

    std::vector<u64>& back_buffer = *pback_buffer;
    LaneDigitArray& counts = *pcounts;
    LaneDigitArray& offsets = *poffsets;

    for (u32 byte_idx = 0; byte_idx < 8; byte_idx++)
    {
        counts[lane_idx].fill(0);

        for (u64 i = lane_start; i < lane_end; i++)
        {
            u8 digit = (arr[i] >> (8*byte_idx)) & 0xFF;
            counts[lane_idx][digit] += 1;
        }

        ParallelAlgorithm::LaneSync();

        // calculate the relative offsets in parallel
        Range digit_chunk = ParallelAlgorithm::ChunkForLane(256);
        for (u32 digit = digit_chunk.start; digit < digit_chunk.end; digit++)
        {
            u32 total = 0;
            for (u32 lane = 0; lane < lane_count; lane++)
            {
                offsets[lane][digit] = total;
                total += counts[lane][digit];
            }
        }

        ParallelAlgorithm::LaneSync();

        // combine the offsets (single threaded)
        if (lane_idx == 0)
        {
            for (u32 digit = 1; digit < 256; digit++)
            {
                u32 increment = offsets[lane_count-1][digit-1] + counts[lane_count-1][digit-1];
                for (u32 lane = 0; lane < lane_count; lane++)
                {
                    offsets[lane][digit] += increment;
                }
            }
        }

        ParallelAlgorithm::LaneSync();

        for (u64 i = lane_start; i < lane_end; i++)
        {
            u32 digit = (arr[i] >> (8*byte_idx)) & 0xFF;
            u32 write_idx = offsets[lane_idx][digit]++;
            back_buffer[write_idx] = arr[i];
        }

        // make sure the counting sort pass is completed before swapping the back_buffer
        ParallelAlgorithm::LaneSync();
        if (lane_idx == 0)
        {
            back_buffer.swap(arr);
        }
        // make sure the swap is visible to every lane before the next pass reads arr/back_buffer
        ParallelAlgorithm::LaneSync();
    }

    if (lane_idx == 0)
    {
        delete pcounts;
        delete poffsets;
        delete pback_buffer;
    }
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& v) {
    os << "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        os << v[i];
        if (i + 1 != v.size()) {
            os << ", ";
        }
    }
    os << "]";
    return os;
}

template <typename T>
void PrintVector(const std::vector<T>& v, std::ostream& os = std::cout) {
    os << v << "\n";
}

int main(void)
{
    ParallelAlgorithm parallel;

    std::vector<u64> arr = {1, 2,3,1,1,1,2,3,3};

    PrintVector(arr);

    parallel.Execute(ParallelRadixSort, arr);

    PrintVector(arr);
    return 0;
}

