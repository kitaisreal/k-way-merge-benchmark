#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

/** Binary heap with bottom-up replace top (Wegener bottom-up heapsort trick, the same one
  * libstdc++ uses inside std::priority_queue pop). Classic sift down asks two questions per level:
  * "which child is smaller" and "does the new element stop here". For a random element the second
  * answer is almost always "no" - half of the heap nodes are leaves, so the element almost always
  * sinks to the bottom, and those questions are wasted comparisons.
  *
  * Bottom-up variant sinks the hole to the bottom asking only "which child is smaller" - one
  * comparison per level - then bubbles the new element up from the leaf, which for a random element
  * stops after O(1) expected comparisons. ~log2(K) + O(1) comparisons per relocation instead of
  * ~2 * log2(K) of the classic sift down.
  *
  * The adaptive early exit is the same as in the classic heap: one comparison against the smaller
  * child of the root decides whether the advanced cursor is still the minimum. So this heap makes
  * both bets: "element stays on top" (wins on runs) and "element sinks to the bottom" (wins on
  * unique data). The losing spot is in between - when the element belongs a few levels below the
  * root, the hole still travels all the way down and the element bubbles almost all the way up.
  */
template <typename Cursor>
class HeapBottomUpSortingQueue
{
public:
    HeapBottomUpSortingQueue() = default;

    template <typename Cursors>
    explicit HeapBottomUpSortingQueue(Cursors & cursors)
    {
        size_t size = cursors.size();
        queue.reserve(size);

        for (size_t i = 0; i < size; ++i)
        {
            if (cursors[i].isEmpty())
                continue;

            queue.emplace_back(cursors[i]);
        }

        std::make_heap(queue.begin(), queue.end(), InvertedLess{});
    }

    bool isValid() const { return !queue.empty(); }

    Cursor & current() { return queue.front(); }

    void next()
    {
        assert(isValid());

        if (!queue.front().isLast())
        {
            queue.front().next();
            updateTop();
        }
        else
        {
            removeTop();
        }
    }

    void removeTop()
    {
        std::pop_heap(queue.begin(), queue.end(), InvertedLess{});
        queue.pop_back();
        next_child_idx = 0;
    }

private:
    /** std::make_heap and std::pop_heap build max-heap relative to provided comparator.
      * Comparing through inverted "less" turns it into min-heap that is needed for merge.
      */
    struct InvertedLess
    {
        bool operator()(const Cursor & lhs, const Cursor & rhs) const { return rhs.less(lhs); }
    };

    using Container = std::vector<Cursor>;
    Container queue;

    /// Cache comparison between first and second child if the order in queue has not been changed.
    size_t next_child_idx = 0;

    size_t nextChildIndex()
    {
        if (next_child_idx == 0)
        {
            next_child_idx = 1;

            if (queue.size() > 2 && queue[2].less(queue[1]))
                ++next_child_idx;
        }

        return next_child_idx;
    }

    void updateTop()
    {
        size_t size = queue.size();
        if (size < 2)
            return;

        size_t child_idx = nextChildIndex();

        /// Check if we are in order.
        if (queue[0].less(queue[child_idx]))
            return;

        next_child_idx = 0;
        auto top(std::move(queue[0]));

        /// Sink the hole to the bottom, one "which child is smaller" comparison per level.
        /// The smaller child of the root is already known from the early exit check.
        size_t hole = 0;
        while (true)
        {
            queue[hole] = std::move(queue[child_idx]);
            hole = child_idx;

            child_idx = (2 * hole) + 1;
            if (child_idx >= size)
                break;

            if ((child_idx + 1) < size && queue[child_idx + 1].less(queue[child_idx]))
                ++child_idx;
        }

        /// Bubble the new element up from the leaf, for a random element stops after O(1) levels.
        while (hole != 0)
        {
            size_t parent = (hole - 1) / 2;
            if (!top.less(queue[parent]))
                break;

            queue[hole] = std::move(queue[parent]);
            hole = parent;
        }

        queue[hole] = std::move(top);
    }
};
