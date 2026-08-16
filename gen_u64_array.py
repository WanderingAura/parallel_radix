#!/usr/bin/env python3
"""Generates a binary file of little-endian uint64 values.

Usage:
    python3 gen_u64_array.py OUTPUT COUNT [--max-value MAX] [--seed SEED]

Example:
    python3 gen_u64_array.py data.bin 1000000 --max-value 1000
"""
import argparse
import random
import struct
import sys

U64_MAX = 2**64 - 1


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output", help="path to write the binary file to")
    parser.add_argument("count", type=int, help="number of u64 elements to generate")
    parser.add_argument("--max-value", type=int, default=U64_MAX,
                         help=f"maximum value (inclusive) for each element, default {U64_MAX}")
    parser.add_argument("--seed", type=int, default=None, help="random seed for reproducible output")
    args = parser.parse_args(argv)

    if args.count < 0:
        parser.error("count must be non-negative")
    if not (0 <= args.max_value <= U64_MAX):
        parser.error(f"max-value must be between 0 and {U64_MAX}")

    return args


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    rng = random.Random(args.seed)

    with open(args.output, "wb") as f:
        for _ in range(args.count):
            f.write(struct.pack("<Q", rng.randint(0, args.max_value)))

    print(f"wrote {args.count} u64 elements to {args.output}")


if __name__ == "__main__":
    main()
