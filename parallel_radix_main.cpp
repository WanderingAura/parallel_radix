#include <iostream>
#include <vector>
#include <array>
#include <cstring>
#include <cassert>
#include <fstream>
#include <string>
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

// unity build cos i'm lazy
#include "parallel_algorithm.cpp"

// running timing stats (microseconds) across ParallelRadixSort calls
static u64 g_total_time_us = 0;
static u64 g_min_time_us = UINT64_MAX;
static u64 g_max_time_us = 0;

void ParallelRadixSort(std::vector<u64>& arr)
{
    u32 lane_count = ParallelAlgorithm::LaneCount();
    u32 lane_idx = ParallelAlgorithm::LaneIdx();
    u64 n = arr.size();

    // fall back to lane 0 only if there's no other lane to measure from
    u32 timing_lane = (lane_count > 1) ? 1 : 0;
    std::chrono::steady_clock::time_point timing_start;
    if (lane_idx == timing_lane)
    {
        timing_start = std::chrono::steady_clock::now();
    }

    Range chunk = ParallelAlgorithm::ChunkForLane(n);
    u64 lane_start = chunk.start;
    u64 lane_end = chunk.end;

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

        // TODO: during the first pass we can find the max element of the array
        // and decide how many counting sort passes we do based on that, saving
        // us from doing unnecessary count sort passes
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

    if (lane_idx == timing_lane)
    {
        auto timing_end = std::chrono::steady_clock::now();
        u64 elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(timing_end - timing_start).count();

        g_total_time_us += elapsed_us;
        g_min_time_us = std::min(g_min_time_us, elapsed_us);
        g_max_time_us = std::max(g_max_time_us, elapsed_us);
    }

    if (lane_idx == 0)
    {
        delete pcounts;
        delete poffsets;
        delete pback_buffer;
    }
}

// reads a binary file of little-endian u64 values (as written by gen_u64_array.py) into a vector
std::vector<u64> ReadU64VectorFromFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("failed to open file: " + path);
    }

    std::streamsize size = file.tellg();
    if (size < 0 || size % sizeof(u64) != 0)
    {
        throw std::runtime_error("file size is not a multiple of sizeof(u64): " + path);
    }
    file.seekg(0, std::ios::beg);

    std::vector<u64> result(static_cast<size_t>(size) / sizeof(u64));
    if (!result.empty() && !file.read(reinterpret_cast<char*>(result.data()), size))
    {
        throw std::runtime_error("failed to read file: " + path);
    }

    return result;
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

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " <num_runs>\n";
        return 1;
    }

    int n_runs = std::atoi(argv[1]);
    if (n_runs <= 0)
    {
        std::cerr << "num_runs must be a positive integer\n";
        return 1;
    }

    ParallelAlgorithm parallel;

    std::vector<u64> original = ReadU64VectorFromFile("data.bin");
    std::cout << "loaded " << original.size() << " elements\n";

    for (int run = 0; run < n_runs; run++)
    {
        std::vector<u64> arr = original;
        parallel.Execute(ParallelRadixSort, arr);
    }

    std::cout << n_runs << " runs -- total: " << (g_total_time_us / 1000.0) << " ms"
               << ", avg: " << (g_total_time_us / 1000.0 / n_runs) << " ms"
               << ", min: " << (g_min_time_us / 1000.0) << " ms"
               << ", max: " << (g_max_time_us / 1000.0) << " ms\n";

    return 0;
}

