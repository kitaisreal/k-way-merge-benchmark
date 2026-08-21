#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "AbseilBTree.h"
#include "BTree.h"
#include "Heap.h"
#include "HeapBottomUp.h"
#include "ImplicitTreap.h"
#include "IntegerArrayCursor.h"
#include "LoserTree.h"
#include "SortedArray.h"
#include "StdSet.h"
#include "Utils.h"

namespace
{

/// Values are uniform in [0, cardinality), so cardinality controls the amount of duplicates:
/// 1 - all values are equal, elements size - all values are almost surely unique.
std::vector<uint64_t> generateValues(size_t elements_size, size_t cardinality, uint64_t seed)
{
    std::mt19937_64 generator(seed);
    std::uniform_int_distribution<uint64_t> distribution(0, static_cast<uint64_t>(cardinality) - 1);

    std::vector<uint64_t> values(elements_size);
    for (auto & value : values)
        value = distribution(generator);

    return values;
}

/** Layout controls how value ranges of cursors intersect:
  * - random: values are split into cursors in generation order, every cursor covers the whole
  *   value range, maximum interleaving during merge;
  * - disjoint: values are sorted before the split, cursor ranges do not intersect (LSM-like case
  *   where old parts hold old keys), merge degenerates into concatenation.
  * Values inside each cursor are sorted with raw int64 comparisons that are not counted.
  */
std::vector<IntegerArrayCursor> buildSortedCursors(std::vector<uint64_t> & values, size_t cursors_size, const std::string & layout)
{
    if (layout == "disjoint")
        std::ranges::sort(values);

    size_t elements_size = values.size();
    size_t cursor_size = elements_size / cursors_size;
    std::vector<IntegerArrayCursor> cursors;

    for (size_t i = 0; i < cursors_size - 1; ++i)
    {
        size_t from = i * cursor_size;
        size_t to = (i * cursor_size) + cursor_size;
        std::sort(values.begin() + from, values.begin() + to);
        cursors.emplace_back(values.data() + from, cursor_size, i);
    }

    size_t last_cursor_start_offset = (cursors_size - 1) * cursor_size;
    size_t last_cursor_size = elements_size - last_cursor_start_offset;
    std::sort(values.begin() + last_cursor_start_offset, values.end());
    cursors.emplace_back(values.data() + last_cursor_start_offset, last_cursor_size, cursors.size());

    return cursors;
}

/** Two modes:
  * - generator: --K, --N and --C are required, values are uniform in [0, C);
  * - file: --K and --file are required, the whole file is read and split into contiguous
  *   ranges in file order, --N, --C, --layout and --seed are not applicable.
  */
struct Arguments
{
    std::string strategy;
    size_t cursors_size = 0;
    size_t elements_size = 0;
    size_t cardinality = 0;
    std::string file;
    std::string layout = "random";
    uint64_t seed = 42;
};

void printUsage()
{
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  ./k_way_merge_benchmark --strategy <strategy> --K <cursors> --N <elements> --C <cardinality> \\" << std::endl;
    std::cerr << "      [--layout random|disjoint] [--seed 42]" << std::endl;
    std::cerr << "  ./k_way_merge_benchmark --strategy <strategy> --K <cursors> --file <path>" << std::endl;
    std::cerr << "Arguments:" << std::endl;
    std::cerr << "  --strategy: heap, heap_bottom_up, loser_tree, btree, abseil_btree, sorted_array, implicit_treap, std_set" << std::endl;
    std::cerr << "  --K: cursors count" << std::endl;
    std::cerr << "  --N: elements count" << std::endl;
    std::cerr << "  --C: values cardinality, values are uniform in [0, C)" << std::endl;
    std::cerr << "  --file: UInt64 column exported from ClickHouse in RowBinaryWithNamesAndTypes format" << std::endl;
    std::cerr << "  --layout: random - cursors cover the whole value range, disjoint - cursor ranges do not intersect" << std::endl;
}

bool parseUInt64(const char * value, uint64_t & result)
{
    try
    {
        size_t parsed_size = 0;
        result = std::stoull(value, &parsed_size);
        return parsed_size == std::strlen(value);
    }
    catch (...)
    {
        return false;
    }
}

bool parseArguments(int argc, char ** argv, Arguments & arguments)
{
    bool has_elements = false;
    bool has_cardinality = false;
    bool has_layout = false;
    bool has_seed = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string name = argv[i];

        if (i + 1 >= argc)
        {
            std::cerr << "Missing value for argument " << name << std::endl;
            return false;
        }

        const char * value = argv[++i];
        uint64_t number = 0;
        bool is_number = parseUInt64(value, number);

        if (name == "--strategy" || name == "-s")
        {
            arguments.strategy = value;
        }
        else if (name == "--K" || name == "-K")
        {
            if (!is_number || number == 0)
            {
                std::cerr << "Invalid " << name << " value " << value << ", expected positive integer" << std::endl;
                return false;
            }
            arguments.cursors_size = number;
        }
        else if (name == "--N" || name == "-N")
        {
            if (!is_number || number == 0)
            {
                std::cerr << "Invalid " << name << " value " << value << ", expected positive integer" << std::endl;
                return false;
            }
            arguments.elements_size = number;
            has_elements = true;
        }
        else if (name == "--C" || name == "-C")
        {
            if (!is_number || number == 0)
            {
                std::cerr << "Invalid " << name << " value " << value << ", expected positive integer" << std::endl;
                return false;
            }
            arguments.cardinality = number;
            has_cardinality = true;
        }
        else if (name == "--file" || name == "-f")
        {
            arguments.file = value;
        }
        else if (name == "--layout" || name == "-l")
        {
            arguments.layout = value;
            has_layout = true;
        }
        else if (name == "--seed")
        {
            if (!is_number)
            {
                std::cerr << "Invalid " << name << " value " << value << ", expected integer" << std::endl;
                return false;
            }
            arguments.seed = number;
            has_seed = true;
        }
        else
        {
            std::cerr << "Unknown argument " << name << std::endl;
            return false;
        }
    }

    if (arguments.strategy.empty())
    {
        std::cerr << "Missing required argument --strategy" << std::endl;
        return false;
    }

    if (arguments.cursors_size == 0)
    {
        std::cerr << "Missing required argument --K" << std::endl;
        return false;
    }

    if (arguments.layout != "random" && arguments.layout != "disjoint")
    {
        std::cerr << "Invalid --layout value " << arguments.layout << ", expected random or disjoint" << std::endl;
        return false;
    }

    if (!arguments.file.empty())
    {
        if (has_elements || has_cardinality || has_layout || has_seed)
        {
            std::cerr << "--N, --C, --layout and --seed are not applicable in file mode" << std::endl;
            return false;
        }
        return true;
    }

    if (!has_elements || !has_cardinality)
    {
        std::cerr << "Either --file or both --N and --C must be set" << std::endl;
        return false;
    }

    return true;
}

struct TestStrategyResult
{
    std::vector<uint64_t> values;
    size_t init_comparisons = 0;
    size_t merge_comparisons = 0;
};

template <typename SortQueueType>
TestStrategyResult TestStrategy(const std::vector<IntegerArrayCursor> & cursors)
{
    TestStrategyResult result;

    size_t comparisons_before_init = IntegerArrayCursor::getComparisonsCount();
    SortQueueType sorting_queue(cursors);
    result.init_comparisons = IntegerArrayCursor::getComparisonsCount() - comparisons_before_init;

    size_t comparisons_before_merge = IntegerArrayCursor::getComparisonsCount();
    while (sorting_queue.isValid())
    {
        result.values.push_back(sorting_queue.current().value());
        sorting_queue.next();
    }
    result.merge_comparisons = IntegerArrayCursor::getComparisonsCount() - comparisons_before_merge;

    return result;
}

}

/** Benchmark to compare sorting queues (binary heap, loser tree, b-tree, sorted array, std::set)
  * for k-way merge sort by comparisons count on different data distributions.
  * https://en.wikipedia.org/wiki/K-way_merge_algorithm
  */
int main(int argc, char ** argv)
{
    Arguments arguments;
    if (!parseArguments(argc, argv, arguments))
    {
        printUsage();
        return 1;
    }

    const auto & strategy = arguments.strategy;
    size_t cursors_size = arguments.cursors_size;

    std::vector<uint64_t> values;

    if (!arguments.file.empty())
    {
        values = readValuesFromFile(arguments.file);
        std::cout << "File rows: " << values.size() << std::endl;

        if (values.size() < cursors_size)
        {
            std::cerr << "File must contain at least cursors size values" << std::endl;
            return 1;
        }
    }
    else
    {
        values = generateValues(arguments.elements_size, arguments.cardinality, arguments.seed);
    }

    size_t elements_size = values.size();

    std::cout << "Strategy: " << strategy << std::endl;
    std::cout << "Cursors size: " << cursors_size << std::endl;
    std::cout << "Elements size: " << elements_size << std::endl;
    if (arguments.file.empty())
    {
        std::cout << "Cardinality: " << arguments.cardinality << std::endl;
        std::cout << "Seed: " << arguments.seed << std::endl;
        std::cout << "Layout: " << arguments.layout << std::endl;
    }
    else
    {
        std::cout << "File: " << arguments.file << std::endl;
    }

    auto cursors = buildSortedCursors(values, cursors_size, arguments.layout);

    TestStrategyResult result;

    if (strategy == "heap")
        result = TestStrategy<HeapSortingQueue<IntegerArrayCursor>>(cursors);
    else if (strategy == "heap_bottom_up")
        result = TestStrategy<HeapBottomUpSortingQueue<IntegerArrayCursor>>(cursors);
    else if (strategy == "loser_tree")
        result = TestStrategy<LoserTree<IntegerArrayCursor>>(cursors);
    else if (strategy == "btree")
        result = TestStrategy<BTree<IntegerArrayCursor>>(cursors);
    else if (strategy == "sorted_array")
    {
        /// Narrow order indexes to cursors count: does not change comparisons, shrinks shifts.
        if (cursors_size <= 256)
            result = TestStrategy<SortedArray<IntegerArrayCursor, uint8_t>>(cursors);
        else if (cursors_size <= 65536)
            result = TestStrategy<SortedArray<IntegerArrayCursor, uint16_t>>(cursors);
        else
            result = TestStrategy<SortedArray<IntegerArrayCursor>>(cursors);
    }
    else if (strategy == "std_set")
        result = TestStrategy<StdSet<IntegerArrayCursor>>(cursors);
    else if (strategy == "abseil_btree")
        result = TestStrategy<AbseilBTree<IntegerArrayCursor>>(cursors);
    else if (strategy == "implicit_treap")
        result = TestStrategy<ImplicitTreap<IntegerArrayCursor>>(cursors);
    else
    {
        std::cerr << "Unexpected strategy " << strategy << std::endl;
        return 1;
    }

    std::ranges::sort(values);
    if (values != result.values)
    {
        std::cerr << "Wrong answer" << std::endl;
        return 1;
    }

    size_t init_comparisons = result.init_comparisons;
    size_t merge_comparisons = result.merge_comparisons;
    size_t total_comparisons = init_comparisons + merge_comparisons;

    std::cout << "Init comparisons: " << init_comparisons << std::endl;
    std::cout << "Merge comparisons: " << merge_comparisons << std::endl;
    std::cout << "Total comparisons: " << total_comparisons << std::endl;
    std::cout << "Comparisons per element: " << static_cast<double>(total_comparisons) / static_cast<double>(elements_size) << std::endl;

    return 0;
}
