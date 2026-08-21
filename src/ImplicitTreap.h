#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

/** Sorted array on top of implicit treap (cartesian tree by implicit key) for k-way merge.
  * Tree stores cursor indexes in sorted cursor order, node position works as an array index:
  * random access by position and insert/erase at position cost O(log(K)) node hops.
  *
  * The protocol and comparisons count are exactly the same as SortedArray by construction:
  * one comparison against the element at position 1 decides whether the advanced cursor is still
  * the minimum, otherwise the new position is found by binary search over positions - each probe
  * is a positional access (subtree size navigation, no cursor comparisons) plus one comparison.
  * So relocation costs the same ~log(K) comparisons as sorted array, but O(log(K)^2) node hops
  * of navigation, while moves shrink from O(K) array shift to O(log(K)) node relinks.
  */
template <typename Cursor>
class ImplicitTreap
{
public:
    ImplicitTreap() = default;

    explicit ImplicitTreap(const std::vector<Cursor> & cursors_) : cursors(cursors_), priority_generator(42)
    {
        std::vector<uint32_t> order;
        for (size_t cursor_index = 0; cursor_index < cursors.size(); ++cursor_index)
        {
            if (!cursors[cursor_index].isValid())
                continue;

            order.push_back(static_cast<uint32_t>(cursor_index));
        }

        std::sort(order.begin(), order.end(), [this](uint32_t lhs, uint32_t rhs) { return cursors[lhs].less(cursors[rhs]); });

        nodes.reserve(order.size());
        for (uint32_t cursor_index : order)
            root = merge(root, makeNode(cursor_index));
    }

    bool isValid() const { return root != Null; }

    Cursor & current()
    {
        assert(isValid());
        return cursors[nodes[nodeAtPosition(0)].cursor_index];
    }

    void next()
    {
        assert(isValid());

        uint32_t cursor_index = nodes[nodeAtPosition(0)].cursor_index;
        cursors[cursor_index].next();

        if (!cursors[cursor_index].isValid())
        {
            eraseFront();
            return;
        }

        if (nodes[root].subtree_size == 1)
            return;

        /// Advanced cursor is still the minimum, the tree is untouched.
        if (cursors[cursor_index].less(cursors[nodes[nodeAtPosition(1)].cursor_index]))
            return;

        eraseFront();

        /** Relocate the advanced cursor. It is already known to be greater than the element
          * at position 0 (previous position 1), so binary search runs over positions [1, size).
          * Each probe is a positional access without cursor comparisons plus one comparison.
          */
        size_t low = 1;
        size_t high = nodes[root].subtree_size;
        while (low < high)
        {
            size_t middle = low + ((high - low) / 2);
            if (cursors[nodes[nodeAtPosition(middle)].cursor_index].less(cursors[cursor_index]))
                low = middle + 1;
            else
                high = middle;
        }

        insertAtPosition(low, cursor_index);
    }

private:
    static constexpr int32_t Null = -1;

    struct Node
    {
        uint32_t cursor_index = 0;
        uint32_t priority = 0;
        uint32_t subtree_size = 1;
        int32_t left = Null;
        int32_t right = Null;
    };

    uint32_t subtreeSize(int32_t node) const { return node == Null ? 0 : nodes[node].subtree_size; }

    void update(int32_t node) { nodes[node].subtree_size = subtreeSize(nodes[node].left) + subtreeSize(nodes[node].right) + 1; }

    int32_t makeNode(uint32_t cursor_index)
    {
        Node node;
        node.cursor_index = cursor_index;
        node.priority = static_cast<uint32_t>(priority_generator());

        if (!free_nodes.empty())
        {
            int32_t node_index = free_nodes.back();
            free_nodes.pop_back();
            nodes[node_index] = node;
            return node_index;
        }

        nodes.push_back(node);
        return static_cast<int32_t>(nodes.size() - 1);
    }

    /// Positional access: walk by subtree sizes, O(log(K)) hops, no cursor comparisons.
    int32_t nodeAtPosition(size_t position) const
    {
        int32_t node = root;
        while (true)
        {
            size_t left_size = subtreeSize(nodes[node].left);
            if (position < left_size)
            {
                node = nodes[node].left;
            }
            else if (position == left_size)
            {
                return node;
            }
            else
            {
                position -= left_size + 1;
                node = nodes[node].right;
            }
        }
    }

    /// Split by position: first position nodes go to the left tree.
    void split(int32_t node, size_t position, int32_t & left, int32_t & right)
    {
        if (node == Null)
        {
            left = Null;
            right = Null;
            return;
        }

        size_t left_size = subtreeSize(nodes[node].left);
        if (position <= left_size)
        {
            split(nodes[node].left, position, left, nodes[node].left);
            right = node;
        }
        else
        {
            split(nodes[node].right, position - left_size - 1, nodes[node].right, right);
            left = node;
        }

        update(node);
    }

    int32_t merge(int32_t left, int32_t right)
    {
        if (left == Null)
            return right;
        if (right == Null)
            return left;

        if (nodes[left].priority > nodes[right].priority)
        {
            nodes[left].right = merge(nodes[left].right, right);
            update(left);
            return left;
        }

        nodes[right].left = merge(left, nodes[right].left);
        update(right);
        return right;
    }

    void eraseFront()
    {
        int32_t front = Null;
        split(root, 1, front, root);
        free_nodes.push_back(front);
    }

    void insertAtPosition(size_t position, uint32_t cursor_index)
    {
        int32_t left = Null;
        int32_t right = Null;
        split(root, position, left, right);
        root = merge(merge(left, makeNode(cursor_index)), right);
    }

    std::vector<Cursor> cursors;
    std::vector<Node> nodes;
    std::vector<int32_t> free_nodes;
    std::mt19937 priority_generator;
    int32_t root = Null;
};
