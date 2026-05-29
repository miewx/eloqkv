#include "b255_encode.h"
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <optional>
#include <limits>

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

std::optional<uint64_t> DecodeBase255(std::string_view s)
{
    if (s.empty())
    {
        return std::nullopt;
    }
    uint64_t id = 0;
    for (char c : s)
    {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 1)
        {
            return std::nullopt;
        }
        uint64_t digit = uc - 1;
        if (id > (std::numeric_limits<uint64_t>::max() - digit) / 255)
        {
            return std::nullopt;
        }
        id = id * 255 + digit;
    }
    return id;
}

} // namespace EloqKV
