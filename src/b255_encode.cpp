#include "b255_encode.h"
#include <algorithm>
#include <cstdint>

namespace EloqKV
{

std::string EncodeBase255(uint64_t id)
{
    if (id == 0)
    {
        return std::string(1, '\x01');
    }
    std::string result;
    uint64_t temp = id;
    while (temp > 0)
    {
        uint64_t digit = temp % 255;
        char c = static_cast<char>(digit + 1);
        result.push_back(c);
        temp /= 255;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

uint64_t DecodeBase255(std::string_view s)
{
    uint64_t id = 0;
    for (char c : s)
    {
        uint64_t digit = static_cast<unsigned char>(c) - 1;
        id = id * 255 + digit;
    }
    return id;
}

} // namespace EloqKV
