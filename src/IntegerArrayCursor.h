#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

class IntegerArrayCursor
{
public:
    explicit IntegerArrayCursor(const uint64_t * values_, size_t values_size_, size_t cursor_index_)
        : values(values_), values_size(values_size_), cursor_index(cursor_index_)
    {
    }

    bool isValid() const { return position < values_size; }

    bool isLast() const { return position + 1 >= values_size; }

    bool isEmpty() const { return values_size == 0; }

    void next()
    {
        assert(isValid());
        ++position;
    }

    uint64_t value() const { return values[position]; }

    /** Total order by (value, cursor_index) pair.
      * Tie-break by cursor index makes merge stable: on equal values cursor with lower index wins.
      * This is the only comparison method, every sorting queue must use it, so comparisons counter
      * is the single point of truth for all strategies.
      */
    bool less(const IntegerArrayCursor & rhs) const
    {
        assert(isValid());
        assert(rhs.isValid());

        ++comparisons_count;

        if (values[position] == rhs.values[rhs.position])
            return cursor_index < rhs.cursor_index;

        return values[position] < rhs.values[rhs.position];
    }

    static size_t getComparisonsCount() { return comparisons_count; }

    static void resetComparisonsCount() { comparisons_count = 0; }

private:
    static size_t comparisons_count;
    const uint64_t * values = nullptr;
    size_t values_size = 0;
    size_t position = 0;
    size_t cursor_index = 0;
};
