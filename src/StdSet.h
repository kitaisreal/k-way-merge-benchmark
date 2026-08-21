#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <set>
#include <vector>

/** std::multiset of cursor indexes ordered by Cursor::less for k-way merge - the "just use the
  * standard library" baseline. std::set, std::multiset and std::map are the same red-black tree
  * inside, a set flavor is used to not invent a dummy mapped value.
  *
  * multiset instead of set on purpose: our keys are never equal (total order with tie-break by
  * cursor index inside Cursor::less), and _M_insert_unique of std::set spends one extra comparison
  * per insert on the uniqueness check that _M_insert_equal does not need (measured: exactly 1.0
  * comparison per element on unique values).
  *
  * next() is adaptive the same way heap is: the successor of the minimum is std::next(begin()) -
  * no comparisons, then one comparison decides whether the advanced cursor is still the minimum.
  * If it is, the tree is untouched: the stored key is a cursor index and its relative order did not
  * change, so tree invariants still hold. Otherwise the minimum is erased by iterator (structural,
  * no comparisons) and the cursor is reinserted. Reinsertion costs the red-black tree descent -
  * up to 2 * log2(k) comparisons, since unlike BTree or SortedArray the red-black tree is not
  * perfectly balanced. The advanced cursor is outside the tree during the descent, so every
  * comparison is played against valid keys.
  */
template <typename Cursor>
class StdSet
{
public:
    StdSet() = default;

    explicit StdSet(const std::vector<Cursor> & cursors_) : cursors(cursors_), order(CursorLess{&cursors})
    {
        for (size_t cursor_index = 0; cursor_index < cursors.size(); ++cursor_index)
        {
            if (!cursors[cursor_index].isValid())
                continue;

            order.insert(cursor_index);
        }
    }

    bool isValid() const { return !order.empty(); }

    Cursor & current()
    {
        assert(isValid());
        return cursors[*order.begin()];
    }

    void next()
    {
        assert(isValid());

        auto min_it = order.begin();
        size_t cursor_index = *min_it;
        cursors[cursor_index].next();

        if (!cursors[cursor_index].isValid())
        {
            order.erase(min_it);
            return;
        }

        auto successor_it = std::next(min_it);
        if (successor_it == order.end())
            return;

        /// Advanced cursor is still the minimum, the tree is untouched.
        if (cursors[cursor_index].less(cursors[*successor_it]))
            return;

        order.erase(min_it);
        order.insert(cursor_index);
    }

private:
    struct CursorLess
    {
        const std::vector<Cursor> * cursors = nullptr;

        bool operator()(size_t lhs, size_t rhs) const { return (*cursors)[lhs].less((*cursors)[rhs]); }
    };

    /// Declaration order matters: order comparator points into cursors.
    std::vector<Cursor> cursors;
    std::multiset<size_t, CursorLess> order;
};
