#include "Utils.h"

#include <fstream>
#include <iostream>

namespace
{

bool readVarUInt(std::istream & stream, uint64_t & value)
{
    value = 0;
    for (size_t i = 0; i < 10; ++i)
    {
        int byte = stream.get();
        if (byte == EOF)
            return false;

        value |= static_cast<uint64_t>(byte & 0x7F) << (7 * i);
        if ((byte & 0x80) == 0)
            return true;
    }
    return false;
}

bool readBinaryString(std::istream & stream, std::string & value)
{
    uint64_t size = 0;
    if (!readVarUInt(stream, size))
        return false;

    value.resize(size);
    return static_cast<bool>(stream.read(value.data(), static_cast<std::streamsize>(size)));
}

}

std::vector<uint64_t> readValuesFromFile(const std::string & path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        std::cerr << "Cannot open file " << path << std::endl;
        return {};
    }

    uint64_t columns_size = 0;
    if (!readVarUInt(stream, columns_size) || columns_size != 1)
    {
        std::cerr << "Expected single column in file " << path << std::endl;
        return {};
    }

    std::string column_name;
    std::string column_type;
    if (!readBinaryString(stream, column_name) || !readBinaryString(stream, column_type))
    {
        std::cerr << "Invalid RowBinaryWithNamesAndTypes header in file " << path << std::endl;
        return {};
    }

    if (column_type != "UInt64")
    {
        std::cerr << "Expected UInt64 column in file " << path << ", got " << column_type << std::endl;
        return {};
    }

    auto values_begin_position = stream.tellg();
    stream.seekg(0, std::ios::end);
    size_t values_bytes = static_cast<size_t>(stream.tellg() - values_begin_position);
    stream.seekg(values_begin_position);

    std::vector<uint64_t> values;
    values.reserve(values_bytes / sizeof(uint64_t));

    uint64_t value = 0;
    while (stream.read(reinterpret_cast<char *>(&value), sizeof(value)))
        values.push_back(value);

    return values;
}
