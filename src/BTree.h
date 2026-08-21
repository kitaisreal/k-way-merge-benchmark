#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>


/** B-tree of cursors ordered by Cursor::less for k-way merge.
  *
  * This is a classic B-tree (keys are stored in internal nodes too, not a B+-tree), so every key
  * is stored exactly once and always refers to a live cursor with its current head value. There are
  * no separator copies that could become stale when a cursor advances.
  *
  * Keys are cursor indexes, node capacity is [MinDegree - 1, 2 * MinDegree - 1] keys (root can have
  * less). Comparisons are spent only in binary searches during insert descent, ~log2(k) per
  * relocation. Structural operations - split, borrow, merge and remove of the minimum - are purely
  * positional and cost no comparisons.
  *
  * next() is adaptive the same way heap is: the successor of the minimum is found structurally
  * (second key of the leftmost leaf, or the deepest separator on the leftmost spine - no
  * comparisons), then one comparison decides whether the advanced cursor is still the minimum.
  * If it is, the tree is not modified at all. Otherwise the minimum is removed (no comparisons)
  * and the cursor is reinserted through normal descent. The advanced cursor is outside the tree
  * during the descent, so every comparison is played against valid keys.
  */
template <typename Cursor, size_t MinDegree = 4>
class BTree
{
    static_assert(MinDegree >= 2, "B-tree minimum degree must be at least 2");

public:
    BTree() = default;

    explicit BTree(const std::vector<Cursor> & cursors_) : cursors(cursors_)
    {
        for (size_t cursor_index = 0; cursor_index < cursors.size(); ++cursor_index)
        {
            if (!cursors[cursor_index].isValid())
                continue;

            insert(cursor_index);
        }
    }

    bool isValid() const { return root && !root->keys.empty(); }

    Cursor & current()
    {
        assert(isValid());
        return cursors[minCursorIndex()];
    }

    void next()
    {
        assert(isValid());

        size_t cursor_index = minCursorIndex();
        cursors[cursor_index].next();

        if (!cursors[cursor_index].isValid())
        {
            removeMin();
            return;
        }

        size_t successor_index = successorOfMin();
        if (successor_index == InvalidCursorIndex)
            return;

        /// Advanced cursor is still the minimum, the tree is untouched.
        if (cursors[cursor_index].less(cursors[successor_index]))
            return;

        removeMin();
        insert(cursor_index);
    }

private:
    static constexpr size_t MaxKeys = (2 * MinDegree) - 1;
    static constexpr size_t InvalidCursorIndex = static_cast<size_t>(-1);

    struct Node
    {
        std::vector<size_t> keys;
        std::vector<std::unique_ptr<Node>> children;

        bool isLeaf() const { return children.empty(); }
    };

    /// Minimum is the first key of the leftmost leaf. Walk costs no comparisons.
    size_t minCursorIndex() const
    {
        Node * node = root.get();
        while (!node->isLeaf())
            node = node->children.front().get();

        return node->keys.front();
    }

    /** Successor of the minimum, found structurally without comparisons: second key of the leftmost
      * leaf if it exists, otherwise the deepest separator on the leftmost spine (in-order successor
      * of the last leaf key is the parent separator). InvalidCursorIndex if the tree holds a single key.
      */
    size_t successorOfMin() const
    {
        size_t successor_index = InvalidCursorIndex;

        Node * node = root.get();
        while (!node->isLeaf())
        {
            successor_index = node->keys.front();
            node = node->children.front().get();
        }

        if (node->keys.size() >= 2)
            successor_index = node->keys[1];

        return successor_index;
    }

    /// Position of the first key greater than the cursor. Binary search is the only place
    /// where insertion spends comparisons.
    size_t upperBound(const std::vector<size_t> & keys, size_t cursor_index) const
    {
        auto it = std::upper_bound(
            keys.begin(), keys.end(), cursor_index, [this](size_t lhs, size_t rhs) { return cursors[lhs].less(cursors[rhs]); });
        return it - keys.begin();
    }

    /** Split the full child (2 * MinDegree - 1 keys) of a non-full parent: median key moves up
      * into the parent, right halves of keys and children move into the new right sibling.
      * Purely positional, no comparisons.
      */
    void splitChild(Node & parent, size_t child_index)
    {
        Node & child = *parent.children[child_index];
        assert(child.keys.size() == MaxKeys);

        auto right = std::make_unique<Node>();
        right->keys.assign(child.keys.begin() + MinDegree, child.keys.end());

        size_t median_key = child.keys[MinDegree - 1];
        child.keys.resize(MinDegree - 1);

        if (!child.isLeaf())
        {
            right->children.assign(
                std::make_move_iterator(child.children.begin() + MinDegree), std::make_move_iterator(child.children.end()));
            child.children.resize(MinDegree);
        }

        parent.keys.insert(parent.keys.begin() + child_index, median_key);
        parent.children.insert(parent.children.begin() + child_index + 1, std::move(right));
    }

    /// Standard iterative insert with proactive splitting: every visited node is non-full,
    /// so a single top-down pass suffices.
    void insert(size_t cursor_index)
    {
        if (!root)
            root = std::make_unique<Node>();

        if (root->keys.size() == MaxKeys)
        {
            auto new_root = std::make_unique<Node>();
            new_root->children.push_back(std::move(root));
            root = std::move(new_root);
            splitChild(*root, 0);
        }

        Node * node = root.get();
        while (true)
        {
            size_t position = upperBound(node->keys, cursor_index);

            if (node->isLeaf())
            {
                node->keys.insert(node->keys.begin() + position, cursor_index);
                return;
            }

            if (node->children[position]->keys.size() == MaxKeys)
            {
                splitChild(*node, position);

                /// Median moved up into node->keys[position], decide which half to descend into.
                if (cursors[node->keys[position]].less(cursors[cursor_index]))
                    ++position;
            }

            node = node->children[position].get();
        }
    }

    /** Refill the minimal leftmost child (MinDegree - 1 keys) of the parent before descending into it:
      * either borrow the smallest key of the right sibling through the separator, or merge the child,
      * the separator and the right sibling into one node. Purely positional, no comparisons.
      */
    void fixLeftmostChild(Node & parent)
    {
        Node & child = *parent.children[0];
        Node & right = *parent.children[1];

        if (right.keys.size() >= MinDegree)
        {
            child.keys.push_back(parent.keys.front());
            parent.keys.front() = right.keys.front();
            right.keys.erase(right.keys.begin());

            if (!child.isLeaf())
            {
                child.children.push_back(std::move(right.children.front()));
                right.children.erase(right.children.begin());
            }
        }
        else
        {
            child.keys.push_back(parent.keys.front());
            child.keys.insert(child.keys.end(), right.keys.begin(), right.keys.end());

            if (!child.isLeaf())
            {
                child.children.insert(
                    child.children.end(), std::make_move_iterator(right.children.begin()), std::make_move_iterator(right.children.end()));
            }

            parent.keys.erase(parent.keys.begin());
            parent.children.erase(parent.children.begin() + 1);
        }
    }

    /** Remove the minimum - the first key of the leftmost leaf. Single top-down pass along
      * the leftmost spine with proactive refill, so the leaf can always afford losing a key.
      * No comparisons anywhere on this path.
      */
    void removeMin()
    {
        Node * node = root.get();

        while (!node->isLeaf())
        {
            if (node->children.front()->keys.size() < MinDegree)
                fixLeftmostChild(*node);

            if (node == root.get() && node->keys.empty())
            {
                /// Merge consumed the last separator of the root, tree height shrinks.
                root = std::move(node->children.front());
                node = root.get();
                continue;
            }

            node = node->children.front().get();
        }

        node->keys.erase(node->keys.begin());
    }

    std::unique_ptr<Node> root;
    std::vector<Cursor> cursors;
};
