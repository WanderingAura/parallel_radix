// Test runner for ParallelRadixSort. Pulls in main.cpp directly (renaming its main() out of
// the way) so this exercises the real implementation, not a copy of it.
#define main main_unused_original_entry_point
#include "../main.cpp"
#undef main

#include "test_cases.h"
#include <iostream>

int main()
{
    int failures = 0;
    std::vector<TestCase> cases = GetTestCases();

    for (auto& tc : cases)
    {
        std::vector<u64> arr = tc.input;

        ParallelAlgorithm parallel;
        parallel.Execute(ParallelRadixSort, arr);

        bool ok = (arr == tc.expected);
        std::cout << (ok ? "PASS" : "FAIL") << "  " << tc.name << " (n=" << tc.input.size() << ")\n";

        if (!ok)
        {
            failures++;
            u64 mismatches = 0;
            for (std::size_t i = 0; i < arr.size() && mismatches < 5; i++)
            {
                if (arr[i] != tc.expected[i])
                {
                    std::cout << "    mismatch at index " << i << ": got " << arr[i]
                              << " expected " << tc.expected[i] << "\n";
                    mismatches++;
                }
            }
        }
    }

    std::cout << failures << " failing test(s) out of " << cases.size() << "\n";
    return failures == 0 ? 0 : 1;
}
