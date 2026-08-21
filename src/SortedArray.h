#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <vector>


/** Sorted array of cursors ordered by Cursor::less for k-way merge. The minimum is the front element.
  *
  * next() is adaptive the same way heap is: one comparison against the second element decides
  * whether the advanced cursor is still the minimum, and if it is, the array is untouched.
  * Otherwise the new position is found by binary search over the elements the advanced cursor
  * is already known to be greater than, ~log2(k) comparisons, and the prefix is shifted by one.
  * Shifts are positional and cost no comparisons.
  *
  * IndexType narrows the order array elements: uint8_t for up to 256 cursors keeps the whole
  * array in 4 cache lines, uint16_t covers up to 65536 cursors. Comparisons count does not depend
  * on it, only memory traffic of shifts does.
  */
template <typename Cursor, typename IndexType = size_t>
class SortedArray
{
    static_assert(std::is_unsigned_v<IndexType>);

public:
    SortedArray() = default;

    explicit SortedArray(const std::vector<Cursor> & cursors_) : cursors(cursors_)
    {
        assert(cursors.empty() || cursors.size() - 1 <= std::numeric_limits<IndexType>::max());

        for (size_t cursor_index = 0; cursor_index < cursors.size(); ++cursor_index)
        {
            if (!cursors[cursor_index].isValid())
                continue;

            order.push_back(static_cast<IndexType>(cursor_index));
        }

        std::sort(order.begin(), order.end(), [this](IndexType lhs, IndexType rhs) { return cursors[lhs].less(cursors[rhs]); });
    }

    bool isValid() const { return !order.empty(); }

    Cursor & current()
    {
        assert(isValid());
        return cursors[order.front()];
    }

    void next()
    {
        assert(isValid());

        IndexType cursor_index = order.front();
        cursors[cursor_index].next();

        if (!cursors[cursor_index].isValid())
        {
            order.erase(order.begin());
            return;
        }

        if (order.size() == 1)
            return;

        /// Advanced cursor is still the minimum, the array is untouched.
        if (cursors[cursor_index].less(cursors[order[1]]))
            return;

        /** Relocate the advanced cursor. It is already known to be greater than order[1],
          * so binary search runs over order[2..end) to not spend a comparison on order[1] again.
          */
        auto search_begin = order.begin() + 2;
        auto position = std::upper_bound(
            search_begin, order.end(), cursor_index, [this](IndexType lhs, IndexType rhs) { return cursors[lhs].less(cursors[rhs]); });

        std::move(order.begin() + 1, position, order.begin());
        *(position - 1) = cursor_index;
    }

private:
    /// Indexes of valid cursors sorted ascending by cursor order.
    std::vector<IndexType> order;
    std::vector<Cursor> cursors;
};
