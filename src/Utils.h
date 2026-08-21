#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** Read single UInt64 column exported from ClickHouse in RowBinaryWithNamesAndTypes format:
  * SELECT toUInt64(...) FROM table INTO OUTFILE 'column.bin' FORMAT RowBinaryWithNamesAndTypes
  * SETTINGS max_threads = 1. Values must be order-preserving codes of the original column.
  * Header is varint columns count, then names, then types, then raw 8 byte values.
  * On error prints the reason to stderr and returns empty vector.
  */
std::vector<uint64_t> readValuesFromFile(const std::string & path);
