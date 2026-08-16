#include <cstdint>
#include <functional>
#include <thread>
#include <vector>
#include <barrier>

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