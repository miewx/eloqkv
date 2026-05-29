#pragma once

#include <cstdint>
#include <string>
#include <optional>

namespace EloqKV
{

constexpr char B255_DELIMITER = '\x00';

/**
 * Encodes a numeric namespace ID into a base-255 string using characters starting from \x01.
 * This guarantees the encoded string does not contain any null bytes (\x00), making \x00
 * safe to use as a delimiter.
 */
std::string EncodeBase255(uint64_t id);
std::optional<uint64_t> DecodeBase255(std::string_view s);

} // namespace EloqKV
