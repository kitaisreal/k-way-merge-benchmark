#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>


/// Allows to fetch data from multiple sort cursors in sorted order (merging sorted data streams).
template <typename Cursor>
class HeapSortingQueue
{
public:
    HeapSortingQueue() = default;

    template <typename Cursors>
    explicit HeapSortingQueue(Cursors & cursors)
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

private:
    void removeTop()
    {
        std::pop_heap(queue.begin(), queue.end(), InvertedLess{});
        queue.pop_back();
        next_child_idx = 0;
    }

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

    /// This is adapted version of the function __sift_down from libc++.
    /// Why cannot simply use std::priority_queue?
    /// - because it doesn't support updating the top element and requires pop and push instead.
    /// Also look at "Boost.Heap" library.
    void updateTop()
    {
        size_t size = queue.size();
        if (size < 2)
            return;

        auto begin = queue.begin();

        size_t child_idx = nextChildIndex();
        auto child_it = begin + child_idx;

        /// Check if we are in order.
        if ((*begin).less(*child_it))
            return;

        next_child_idx = 0;
        auto curr_it = begin;
        auto top(std::move(*begin));
        do
        {
            /// We are not in heap-order, swap the parent with it's smallest child.
            *curr_it = std::move(*child_it);
            curr_it = child_it;

            // recompute the child based off of the updated parent
            child_idx = (2 * child_idx) + 1;

            if (child_idx >= size)
                break;

            child_it = begin + child_idx;

            if ((child_idx + 1) < size && (*(child_it + 1)).less(*child_it))
            {
                /// Right child exists and is less than left child.
                ++child_it;
                ++child_idx;
            }

            /// Check if we are in order.
        } while (!(top.less(*child_it)));
        *curr_it = std::move(top);
    }
};
