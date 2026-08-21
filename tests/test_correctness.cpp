#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "AbseilBTree.h"
#include "BTree.h"
#include "Heap.h"
#include "HeapBottomUp.h"
#include "ImplicitTreap.h"
#include "LoserTree.h"
#include "SortedArray.h"
#include "StdSet.h"

namespace
{

/// Cursor that exposes source index so the test can check merge stability, and counts comparisons.
class TestCursor
{
public:
    TestCursor(const std::vector<uint64_t> * values_, size_t cursor_index_) : values(values_), cursor_index(cursor_index_) { }

    bool isValid() const { return position < values->size(); }
    bool isLast() const { return position + 1 >= values->size(); }
    bool isEmpty() const { return values->empty(); }
    void next() { ++position; }
    uint64_t value() const { return (*values)[position]; }
    size_t index() const { return cursor_index; }

    bool less(const TestCursor & rhs) const
    {
        ++comparisons_count;
        if ((*values)[position] == (*rhs.values)[rhs.position])
            return cursor_index < rhs.cursor_index;
        return (*values)[position] < (*rhs.values)[rhs.position];
    }

    static inline size_t comparisons_count = 0;

private:
    const std::vector<uint64_t> * values;
    size_t position = 0;
    size_t cursor_index;
};

using ValueAndSource = std::pair<uint64_t, size_t>;
using Arrays = std::vector<std::vector<uint64_t>>;

/// Trivial reference: repeatedly linear-scan all cursors for min by (value, cursor_index). Obviously correct.
std::vector<ValueAndSource> referenceMerge(const Arrays & arrays)
{
    std::vector<size_t> positions(arrays.size(), 0);
    std::vector<ValueAndSource> result;

    while (true)
    {
        size_t best = SIZE_MAX;
        for (size_t i = 0; i < arrays.size(); ++i)
        {
            if (positions[i] >= arrays[i].size())
                continue;
            if (best == SIZE_MAX || arrays[i][positions[i]] < arrays[best][positions[best]])
                best = i;
        }
        if (best == SIZE_MAX)
            break;
        result.emplace_back(arrays[best][positions[best]], best);
        ++positions[best];
    }
    return result;
}

struct RunResult
{
    std::vector<ValueAndSource> sequence;
    size_t init_comparisons = 0;
    size_t merge_comparisons = 0;
};

template <typename Queue>
RunResult runQueue(const Arrays & arrays)
{
    std::vector<TestCursor> cursors;
    for (size_t i = 0; i < arrays.size(); ++i)
        cursors.emplace_back(&arrays[i], i);

    RunResult result;

    size_t comparisons_before_init = TestCursor::comparisons_count;
    Queue queue(cursors);
    result.init_comparisons = TestCursor::comparisons_count - comparisons_before_init;

    size_t comparisons_before_merge = TestCursor::comparisons_count;
    while (queue.isValid())
    {
        auto & current = queue.current();
        result.sequence.emplace_back(current.value(), current.index());
        queue.next();
    }
    result.merge_comparisons = TestCursor::comparisons_count - comparisons_before_merge;

    return result;
}

size_t failures = 0;

void expect(bool condition, const std::string & message)
{
    if (condition)
        return;

    std::cout << "FAIL " << message << '\n';
    ++failures;
}

/// Runs every strategy on the arrays and checks the full set of invariants.
void testArrays(const Arrays & arrays, const std::string & name)
{
    auto expected = referenceMerge(arrays);

    auto heap = runQueue<HeapSortingQueue<TestCursor>>(arrays);
    auto heap_bottom_up = runQueue<HeapBottomUpSortingQueue<TestCursor>>(arrays);
    auto loser_tree = runQueue<LoserTree<TestCursor>>(arrays);
    auto btree = runQueue<BTree<TestCursor>>(arrays);
    /// Minimal degree forces maximum amount of splits, borrows and merges even on small k.
    auto btree_min_degree = runQueue<BTree<TestCursor, 2>>(arrays);
    auto abseil_btree = runQueue<AbseilBTree<TestCursor>>(arrays);
    auto sorted_array = runQueue<SortedArray<TestCursor>>(arrays);
    auto implicit_treap = runQueue<ImplicitTreap<TestCursor>>(arrays);
    auto std_set = runQueue<StdSet<TestCursor>>(arrays);

    expect(heap.sequence == expected, "heap sequence, " + name);
    expect(heap_bottom_up.sequence == expected, "heap_bottom_up sequence, " + name);
    expect(loser_tree.sequence == expected, "loser_tree sequence, " + name);
    expect(btree.sequence == expected, "btree sequence, " + name);
    expect(btree_min_degree.sequence == expected, "btree min degree sequence, " + name);
    expect(abseil_btree.sequence == expected, "abseil_btree sequence, " + name);
    expect(sorted_array.sequence == expected, "sorted_array sequence, " + name);
    expect(implicit_treap.sequence == expected, "implicit_treap sequence, " + name);

    /// Implicit treap runs the same protocol as sorted array (early exit + positional binary
    /// search over the same ranges), comparisons counters must match exactly.
    expect(
        implicit_treap.init_comparisons == sorted_array.init_comparisons
            && implicit_treap.merge_comparisons == sorted_array.merge_comparisons,
        "implicit_treap comparisons != sorted_array comparisons, " + name);
    expect(std_set.sequence == expected, "std_set sequence, " + name);

    /// Building a tournament of k players costs exactly k - 1 matches.
    size_t valid_cursors = 0;
    for (const auto & array : arrays)
        valid_cursors += !array.empty();
    expect(loser_tree.init_comparisons == (valid_cursors == 0 ? 0 : valid_cursors - 1), "loser_tree init comparisons, " + name);

    /// Order index width must not change comparisons.
    if (arrays.size() <= 256)
    {
        auto narrow = runQueue<SortedArray<TestCursor, uint8_t>>(arrays);
        expect(
            narrow.sequence == expected && narrow.init_comparisons == sorted_array.init_comparisons
                && narrow.merge_comparisons == sorted_array.merge_comparisons,
            "sorted_array narrow index, " + name);
    }

    /// Determinism: the same input must produce identical counters.
    auto heap_again = runQueue<HeapSortingQueue<TestCursor>>(arrays);
    expect(
        heap_again.init_comparisons == heap.init_comparisons && heap_again.merge_comparisons == heap.merge_comparisons,
        "heap determinism, " + name);
}

Arrays generateArrays(std::mt19937 & rng, size_t max_cursors, size_t max_cursor_size, uint64_t value_range)
{
    Arrays arrays(1 + rng() % max_cursors);
    for (auto & array : arrays)
    {
        size_t size = rng() % max_cursor_size;
        for (size_t i = 0; i < size; ++i)
            array.push_back(static_cast<uint64_t>(rng() % value_range));
        std::sort(array.begin(), array.end());
    }
    return arrays;
}

void testEdgeCases()
{
    testArrays({}, "no cursors");
    testArrays({{}}, "single empty cursor");
    testArrays({{}, {}, {}}, "all cursors empty");
    testArrays({{42}}, "single element");
    testArrays({{1, 2, 3}}, "single cursor");
    testArrays({{1, 3}, {}, {2}, {}, {4}}, "empty cursors mixed with non empty");
    testArrays({{1, 2}, {1, 2}, {1, 2}}, "non power of two cursors");

    /// All values equal: output must be whole cursor 0, then whole cursor 1, ... - direct tie-break check.
    {
        Arrays arrays(5, std::vector<uint64_t>(4, 7));
        auto expected = referenceMerge(arrays);
        for (size_t i = 0; i < expected.size(); ++i)
            expect(expected[i].second == i / 4, "reference stability on equal values");
        testArrays(arrays, "all values equal");
    }

    /// Sorted array index width dispatch boundaries.
    std::mt19937 rng(7);
    for (size_t cursors_size : {255, 256, 257})
    {
        Arrays arrays(cursors_size);
        for (auto & array : arrays)
        {
            array = {static_cast<uint64_t>(rng() % 16), 0};
            array[1] = array[0] + static_cast<uint64_t>(rng() % 16);
        }

        auto expected = referenceMerge(arrays);
        auto wide = runQueue<SortedArray<TestCursor>>(arrays);
        auto narrow16 = runQueue<SortedArray<TestCursor, uint16_t>>(arrays);
        expect(wide.sequence == expected, "sorted_array wide at K = " + std::to_string(cursors_size));
        expect(
            narrow16.sequence == expected && narrow16.merge_comparisons == wide.merge_comparisons,
            "sorted_array uint16 at K = " + std::to_string(cursors_size));

        if (cursors_size <= 256)
        {
            auto narrow8 = runQueue<SortedArray<TestCursor, uint8_t>>(arrays);
            expect(
                narrow8.sequence == expected && narrow8.merge_comparisons == wide.merge_comparisons,
                "sorted_array uint8 at K = " + std::to_string(cursors_size));
        }
    }
}

void testRandomized(size_t iterations)
{
    std::mt19937 rng(42);

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        /// Small value range so duplicates are frequent, empty cursors included.
        auto arrays = generateArrays(rng, 24, 12, 1 + rng() % 8);
        testArrays(arrays, "randomized iteration " + std::to_string(iteration));

        if (failures > 5)
            return;
    }
}

}

int main(int argc, char ** argv)
{
    /// Sanitizer builds are an order of magnitude slower, allow to reduce iterations.
    size_t iterations = argc > 1 ? std::stoull(argv[1]) : 10000;

    testEdgeCases();
    testRandomized(iterations);

    if (failures == 0)
    {
        std::cout << "All tests passed" << std::endl;
        return 0;
    }

    return 1;
}
