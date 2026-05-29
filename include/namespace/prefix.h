#pragma once

#include <string>
#include <string_view>
#include "b255_encode.h"

namespace EloqKV
{
namespace NamespacePrefix
{
    constexpr char MAGIC = '\xFF';
    constexpr char VERSION_1 = '\x01';

    // Construct prefix: MAGIC + VERSION_1 + encoded_ns_id + B255_DELIMITER + encoded_epoch + B255_DELIMITER
    inline std::string MakePrefixV1(std::string_view encoded_ns_id, uint64_t epoch)
    {
        std::string prefix;
        prefix.reserve(2 + encoded_ns_id.size() + 1 + 8 + 1);
        prefix.push_back(MAGIC);
        prefix.push_back(VERSION_1);
        prefix.append(encoded_ns_id);
        prefix.push_back(B255_DELIMITER);
        prefix.append(EncodeBase255(epoch));
        prefix.push_back(B255_DELIMITER);
        return prefix;
    }

    // Parse prefix to extract namespace_id, epoch, and original key
    inline bool Parse(std::string_view full_key, std::string_view& ns_id, uint64_t& epoch, std::string_view& user_key)
    {
        if (full_key.size() < 2 || full_key[0] != MAGIC)
        {
            return false;
        }
        char version = full_key[1];
        if (version == VERSION_1)
        {
            size_t ns_start = 2;
            size_t delim1 = full_key.find(B255_DELIMITER, ns_start);
            if (delim1 == std::string_view::npos) return false;

            ns_id = full_key.substr(ns_start, delim1 - ns_start);

            size_t epoch_start = delim1 + 1;
            size_t delim2 = full_key.find(B255_DELIMITER, epoch_start);
            if (delim2 == std::string_view::npos) return false;

            std::string_view encoded_epoch = full_key.substr(epoch_start, delim2 - epoch_start);
            epoch = DecodeBase255(encoded_epoch);

            user_key = full_key.substr(delim2 + 1);
            return true;
        }
        return false;
    }

    // Compose next prefix for range scans (exclusive upper limit)
    inline std::string MakePrefixNext(std::string_view prefix)
    {
        if (prefix.empty())
        {
            return "";
        }
        std::string next_prefix(prefix);
        for (int i = static_cast<int>(next_prefix.size()) - 1; i >= 0; --i)
        {
            auto c = static_cast<unsigned char>(next_prefix[i]);
            if (c != 0xFF)
            {
                next_prefix[i] = static_cast<char>(c + 1);
                next_prefix.resize(i + 1);
                return next_prefix;
            }
        }
        return "";
    }
}
}
