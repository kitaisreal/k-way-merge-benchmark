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

# Rank encoded hits columns, expected in data directory as <column>.bin, see load_data.sh.
COLUMNS = [
    "CounterID",
    "AdvEngineID",
    "TraficSourceID",
    "RegionID",
    "SearchPhrase",
    "URL",
    "UserID",
    "URLHash",
    "EventTime",
    "WatchID",
]

DATA_DIRECTORY = "data"


def parse_argument(argument):
    argument = argument.replace(" ", "")
    if not argument:
        return []
    return argument.split(",")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        prog="KWayMergeBenchmark runner",
        description="Runs k-way merge benchmark on rank encoded hits columns and reports comparisons count",
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
        "--columns",
        "-c",
        default=", ".join(COLUMNS),
        help="Rank encoded hits columns to benchmark",
    )
    parser.add_argument(
        "--data-directory",
        default=DATA_DIRECTORY,
        help="Directory with rank encoded columns, see load_data.sh",
    )
    parser.add_argument(
        "--jobs",
        "-j",
        type=int,
        default=8,
        help="Parallel benchmark runs, comparisons count is deterministic and does not depend on "
        "parallelism, single run takes around 3 GB of memory",
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
    columns = parse_argument(args.columns)
    data_directory = args.data_directory
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

    if not columns:
        print("Invalid input empty columns", file=sys.stderr)
        exit(-1)

    if jobs <= 0:
        print("Invalid jobs, expected positive integer", file=sys.stderr)
        exit(-1)

    if not os.path.isfile(binary):
        print(f"{binary} was not found, build the project first", file=sys.stderr)
        exit(-1)

    for column in columns:
        column_file = os.path.join(data_directory, f"{column}.bin")
        if not os.path.isfile(column_file):
            print(f"{column_file} was not found, run load_data.sh first", file=sys.stderr)
            exit(-1)

    def run_benchmark(cmd):
        output = subprocess.check_output(cmd, shell=True).decode()
        print_debug(f"Output\n{output}")

        for line in output.split("\n"):
            if line.startswith("Comparisons per element: "):
                return float(line.split(": ", 1)[1])

        raise ValueError(f"Invalid output for command {cmd}")

    executor = ThreadPoolExecutor(max_workers=jobs)
    runs = {}

    for column in columns:
        column_file = os.path.join(data_directory, f"{column}.bin")
        for strategy in strategies:
            for cursors_size in cursors_sizes:
                cmd = f"{binary} --strategy {strategy} --K {cursors_size} --file {column_file}"
                runs[(column, strategy, cursors_size)] = executor.submit(run_benchmark, cmd)

    for column in columns:
        results = {
            strategy: {
                cursors_size: runs[(column, strategy, cursors_size)].result()
                for cursors_size in cursors_sizes
            }
            for strategy in strategies
        }

        print(f"Column: {column}\nMetric: comparisons per element")

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
