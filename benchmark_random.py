#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from prettytable import PrettyTable

BENCHMARK_EXECUTABLE = "build/src/k_way_merge_benchmark"

STRATEGIES = ["heap", "heap_bottom_up", "loser_tree", "btree", "abseil_btree", "sorted_array", "implicit_treap", "std_set"]

CURSORS_SIZES = [4, 16, 64, 256, 1024]

# "elements" is resolved to elements size: all values are almost surely unique.
CARDINALITIES = ["1", "10", "1000", "100000", "elements"]

ELEMENTS_SIZES = [10_000_000]

SEED = 42


def parse_argument(argument):
    argument = argument.replace(" ", "")
    if not argument:
        return []
    return argument.split(",")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        prog="KWayMergeBenchmark random runner",
        description="Runs k-way merge benchmark on generated data and reports comparisons count",
    )
    parser.add_argument(
        "--strategies",
        "-s",
        default=", ".join(STRATEGIES),
        help="Strategies to benchmark",
    )
    parser.add_argument(
        "--cursors-sizes",
        "-k",
        default=", ".join(map(str, CURSORS_SIZES)),
        help="Cursors sizes to benchmark",
    )
    parser.add_argument(
        "--cardinalities",
        "-c",
        default=", ".join(CARDINALITIES),
        help="Cardinalities to benchmark, 'elements' resolves to elements size",
    )
    parser.add_argument(
        "--elements-sizes",
        "-n",
        default=", ".join(map(str, ELEMENTS_SIZES)),
        help="Elements sizes to benchmark",
    )
    parser.add_argument("--seed", type=int, default=SEED, help="Random seed")
    parser.add_argument(
        "--jobs",
        "-j",
        type=int,
        default=8,
        help="Parallel benchmark runs, comparisons count is deterministic and does not depend on parallelism",
    )
    parser.add_argument(
        "--binary", "-b", default=BENCHMARK_EXECUTABLE, help="Benchmark executable path"
    )
    parser.add_argument(
        "--debug", action="store_true", help="Run benchmark in debug mode"
    )

    args = parser.parse_args()

    strategies = parse_argument(args.strategies)
    cursors_sizes = [int(value) for value in parse_argument(args.cursors_sizes)]
    cardinalities = parse_argument(args.cardinalities)
    elements_sizes = [int(value) for value in parse_argument(args.elements_sizes)]
    seed = args.seed
    jobs = args.jobs
    binary = args.binary
    debug = args.debug

    def print_debug(*print_args, **print_kwargs):
        if debug:
            print(*print_args, **print_kwargs)

    if not strategies:
        print("Invalid input empty strategies", file=sys.stderr)
        exit(-1)

    if not cursors_sizes:
        print("Invalid input empty cursors sizes", file=sys.stderr)
        exit(-1)

    if not cardinalities:
        print("Invalid input empty cardinalities", file=sys.stderr)
        exit(-1)

    if not elements_sizes or any(value <= 0 for value in elements_sizes):
        print("Invalid elements sizes, expected positive integers", file=sys.stderr)
        exit(-1)

    if jobs <= 0:
        print("Invalid jobs, expected positive integer", file=sys.stderr)
        exit(-1)

    if not os.path.isfile(binary):
        print(f"{binary} was not found, build the project first", file=sys.stderr)
        exit(-1)

    def run_benchmark(cmd):
        output = subprocess.check_output(cmd, shell=True).decode()
        print_debug(f"Output\n{output}")

        for line in output.split("\n"):
            if line.startswith("Comparisons per element: "):
                return float(line.split(": ", 1)[1])

        raise ValueError(f"Invalid output for command {cmd}")

    def resolve_cardinality(elements_size, cardinality):
        return elements_size if cardinality == "elements" else int(cardinality)

    executor = ThreadPoolExecutor(max_workers=jobs)
    runs = {}

    for elements_size in elements_sizes:
        for cardinality in cardinalities:
            resolved_cardinality = resolve_cardinality(elements_size, cardinality)
            for strategy in strategies:
                for cursors_size in cursors_sizes:
                    cmd = (
                        f"{binary} --strategy {strategy} --K {cursors_size} "
                        f"--N {elements_size} --C {resolved_cardinality} --seed {seed}"
                    )
                    runs[(elements_size, cardinality, strategy, cursors_size)] = executor.submit(run_benchmark, cmd)

    for elements_size in elements_sizes:
        for cardinality in cardinalities:
            resolved_cardinality = resolve_cardinality(elements_size, cardinality)

            results = {
                strategy: {
                    cursors_size: runs[(elements_size, cardinality, strategy, cursors_size)].result()
                    for cursors_size in cursors_sizes
                }
                for strategy in strategies
            }

            cardinality_name = (
                f"{resolved_cardinality} (elements)"
                if cardinality == "elements"
                else str(resolved_cardinality)
            )
            print(
                f"Cardinality: {cardinality_name}\n"
                f"Elements size: {elements_size}\nMetric: comparisons per element"
            )

            table = PrettyTable()
            table.field_names = ["Strategy"] + [
                f"K = {cursors_size}" for cursors_size in cursors_sizes
            ]
            table.align["Strategy"] = "l"

            for strategy in strategies:
                row = [strategy] + [
                    f"{results[strategy][cursors_size]:.3f}"
                    for cursors_size in cursors_sizes
                ]
                table.add_row(row)

            print(table)
            print()
