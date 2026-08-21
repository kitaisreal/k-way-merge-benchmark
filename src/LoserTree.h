#pragma once

#include <bit>
#include <cassert>
#include <cstddef>
#include <vector>


/** Implementation of LoserTree (tournament tree variant from Knuth TAOCP vol. 3, 5.4.1) for k-way merge.
  * https://en.wikipedia.org/wiki/K-way_merge_algorithm
  *
  * Each internal node stores the loser of the match played at this node, the overall winner is stored
  * in a separate member. Key invariant: losers stored on the path from the winner's leaf to the root
  * are exactly the winners of sibling subtrees of this path. So when the winner cursor advances,
  * the new candidate replays matches only against values already stored on its path: one node read
  * and one comparison per level. Winner tree variant has identical comparisons count, but its replay
  * reads two children per level instead of one node.
  */
template <typename Cursor>
class LoserTree
{
public:
    LoserTree() : LoserTree({}) { }

    explicit LoserTree(const std::vector<Cursor> & cursors_) : cursors(cursors_)
    {
        if (!cursors.empty())
            buildTree();
    }

    bool isValid() const { return winner != InvalidTreeNodeValue; }

    Cursor & current()
    {
        assert(isValid());
        return cursors[cursorIndexFromTreeNodeValue(winner)];
    }

    void next()
    {
        assert(isValid());

        size_t cursor_index = cursorIndexFromTreeNodeValue(winner);
        cursors[cursor_index].next();
        replayGames(cursor_index);
    }

private:
    using TreeNodeValue = size_t;

    /** Build tree using cursors array.
      * The overall winner is stored in a separate member, the tree itself contains only losers.
      * Tree is stored in flat array of P nodes, where P is cursors count rounded up to power of two.
      * Nodes 1..P-1 form 1-based binary tree of losers (node 0 is unused, 1-based indexing keeps
      * parent/child arithmetic clean): children of node i are 2 * i and 2 * i + 1, parent of node i
      * is i / 2. Leaves are not stored: leaf of cursor i is implicit at index P + i (its current
      * value lives in the cursor itself), so parent of the leaf is (P + i) / 2. The path from any
      * leaf to the root is a statically known chain of index halvings, no comparisons are needed
      * for navigation.
      *
      * Node stores TreeNodeValue - cursor index + 1, or 0 (InvalidTreeNodeValue) for an empty slot:
      * padding leaf (when cursors count is not a power of two), empty or exhausted cursor.
      * This +1 encoding makes zero-initialized array from resize a valid state for empty slots.
      *
      * Build plays the tournament bottom-up through temporary winners array: winners[i] is the winner
      * of the match at node i, the loser is stored into the tree. Matches with at most one valid
      * player are decided without comparison (see playMatch), so build costs exactly
      * (valid cursors - 1) comparisons.
      *
      * Example for 3 cursors A, B, C where B is the overall minimum (P = 4, tree of 4 nodes):
      *     winner: B                  <- separate member
      *     1: loser of final match    <- final: winner of (A, B) vs winner of (C, empty)
      *     2: A          3: empty     <- losers of matches (A vs B), (C vs empty)
      *   [ 4: A   5: B   6: C   7: empty   <- implicit leaf slots, values live in cursors ]
      */
    void buildTree()
    {
        size_t cursors_size = cursors.size();
        size_t nearest_power_of_two = std::bit_ceil(cursors_size);
        loser_tree.resize(nearest_power_of_two);

        std::vector<TreeNodeValue> winners(nearest_power_of_two * 2);

        for (size_t cursor_index = 0; cursor_index < cursors_size; ++cursor_index)
        {
            if (!cursors[cursor_index].isValid())
                continue;

            winners[leafOffset() + cursor_index] = cursorIndexToTreeNodeValue(cursor_index);
        }

        for (size_t node_index = nearest_power_of_two - 1; node_index > 0; --node_index)
            winners[node_index] = playMatch(loser_tree[node_index], winners[node_index * 2], winners[(node_index * 2) + 1]);

        winner = winners[1];
    }

    /** After the winner cursor advanced (or was exhausted), replay matches along the path from its
      * leaf to the root. Losers stored on this path are exactly the winners of sibling subtrees,
      * so the climbing candidate plays each match directly against the stored value: the winner goes
      * up, the loser stays in the node. Costs one comparison per level, log2(P) comparisons total
      * (less if empty slots are on the path).
      */
    void replayGames(size_t cursor_index)
    {
        assert(cursor_index < cursors.size());

        TreeNodeValue candidate = cursors[cursor_index].isValid() ? cursorIndexToTreeNodeValue(cursor_index) : InvalidTreeNodeValue;

        for (size_t node_index = (leafOffset() + cursor_index) >> 1; node_index > 0; node_index >>= 1)
            candidate = playMatch(loser_tree[node_index], loser_tree[node_index], candidate);

        winner = candidate;
    }

    /** Play single match, store the loser into loser_slot and return the winner.
      * If at most one player is valid, bitwise OR picks the winner without spending a comparison:
      * InvalidTreeNodeValue is 0 and 0 | x == x, the loser slot becomes empty. Only a match between
      * two valid players costs one comparison. Merge stability on equal values comes from tie-break
      * inside Cursor::less. Note that lhs is passed by value and is read before loser_slot is
      * written, so passing the same node as loser_slot and lhs (as replayGames does) is safe.
      */
    TreeNodeValue playMatch(TreeNodeValue & loser_slot, TreeNodeValue lhs, TreeNodeValue rhs)
    {
        if (lhs == InvalidTreeNodeValue || rhs == InvalidTreeNodeValue)
        {
            loser_slot = InvalidTreeNodeValue;
            return lhs | rhs;
        }

        bool lhs_wins = cursors[cursorIndexFromTreeNodeValue(lhs)].less(cursors[cursorIndexFromTreeNodeValue(rhs)]);
        loser_slot = lhs_wins ? rhs : lhs;
        return lhs_wins ? lhs : rhs;
    }

    static TreeNodeValue cursorIndexToTreeNodeValue(size_t cursor_index) { return cursor_index + 1; }

    /// For InvalidTreeNodeValue result is garbage (unsigned wraparound), caller must check validity first.
    static size_t cursorIndexFromTreeNodeValue(TreeNodeValue node_value) { return node_value - 1; }

    /// P - cursors count rounded up to power of two, also the index offset of implicit leaf slots.
    size_t leafOffset() const { return loser_tree.size(); }

    static constexpr TreeNodeValue InvalidTreeNodeValue = 0;

    /// Winner of the whole tournament - cursor that holds the current minimum.
    TreeNodeValue winner = InvalidTreeNodeValue;

    std::vector<TreeNodeValue> loser_tree;
    std::vector<Cursor> cursors;
};
