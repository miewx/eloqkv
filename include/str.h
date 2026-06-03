#pragma once

#include <butil/string_piece.h>

#include <cstring>
#include <string_view>
#include <utility>

static inline bool IsEq(butil::StringPiece s1, std::string_view s2)
{
    return s1.size() == s2.size() &&
           strncasecmp(s1.data(), s2.data(), s2.size()) == 0;
}

static inline bool IsEq(std::string_view s1, std::string_view s2)
{
    return s1.size() == s2.size() &&
           strncasecmp(s1.data(), s2.data(), s2.size()) == 0;
}

template <typename... Args>
static inline bool IsEqAny(butil::StringPiece s1, Args &&...args)
{
    return (IsEq(s1, std::forward<Args>(args)) || ...);
}

template <typename... Args>
static inline bool IsEqAny(std::string_view s1, Args &&...args)
{
    return (IsEq(s1, std::forward<Args>(args)) || ...);
}
