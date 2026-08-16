#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <string>
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <cstdint>

typedef uint64_t u64;

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

    std::vector<u64> original = ReadU64VectorFromFile("data.bin");
    std::cout << "loaded " << original.size() << " elements\n";

    u64 total_time_us = 0;
    u64 min_time_us = UINT64_MAX;
    u64 max_time_us = 0;

    for (int run = 0; run < n_runs; run++)
    {
        std::vector<u64> arr = original;

        auto start = std::chrono::steady_clock::now();
        std::sort(arr.begin(), arr.end());
        auto end = std::chrono::steady_clock::now();

        u64 elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        total_time_us += elapsed_us;
        min_time_us = std::min(min_time_us, elapsed_us);
        max_time_us = std::max(max_time_us, elapsed_us);
    }

    std::cout << n_runs << " runs -- total: " << (total_time_us / 1000.0) << " ms"
               << ", avg: " << (total_time_us / 1000.0 / n_runs) << " ms"
               << ", min: " << (min_time_us / 1000.0) << " ms"
               << ", max: " << (max_time_us / 1000.0) << " ms\n";

    return 0;
}
