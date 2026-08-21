#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <vector>

#include <absl/container/btree_set.h>

/** absl::btree_multiset of cursor indexes ordered by Cursor::less for k-way merge.
  *
  * multiset instead of set on purpose: our keys are never equal (total order with tie-break by
  * cursor index inside Cursor::less), and insert_unique of btree_set spends one extra comparison
  * per insert on the uniqueness check that insert_multi does not need.
  *
  * The protocol is the same as StdSet and BTree: the successor of the minimum is std::next(begin())
  * without comparisons, one comparison decides whether the advanced cursor is still the minimum,
  * and if it is, the tree is untouched (the stored key is a cursor index, its relative order did
  * not change, so tree invariants hold). Otherwise the minimum is erased by iterator and the cursor
  * is reinserted through normal descent, which costs binary searches inside nodes on the way down.
  */
template <typename Cursor>
class AbseilBTree
{
public:
    AbseilBTree() = default;

    explicit AbseilBTree(const std::vector<Cursor> & cursors_) : cursors(cursors_), order(CursorLess{&cursors})
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
    absl::btree_multiset<size_t, CursorLess> order;
};
