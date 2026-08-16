# Parallel Radix Sort

An implementation of radix sort inspired by an article on multi-core programming by Ryan Fleury:
https://www.dgtlgrove.com/p/multi-core-by-default

Anything ran using the ParallelAlgorithm class will be ran on all threads, meaning that code that is run by ParallelAlgorithm needs to be quite shader-like.

# Performance

std::sort on 100 million elements:
```bash
$ std_sort_main.exe 10
loaded 100000000 elements
10 runs -- total: 61112.9 ms, avg: 6111.29 ms, min: 6067.41 ms, max: 6168.72 ms
```

parallel_radix_sort on 100 million elements:
```bash
$ parallel_radix_main.exe 10
loaded 100000000 elements
10 runs -- total: 6917.66 ms, avg: 691.766 ms, min: 669.542 ms, max: 734.596 ms
```

On average about 8.8x faster.

