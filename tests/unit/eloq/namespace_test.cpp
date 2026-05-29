#include <iostream>
#include <cassert>
#include <limits>
#include "eloqkv_key.h"
#include "base64url.h"
#include "b255_encode.h"
#include "namespace/prefix.h"

using namespace EloqKV;

void TestBase64AndBase255()
{
    std::cout << "Running TestBase64AndBase255..." << std::endl;

    // Test Base64UrlDecode validation
    uint8_t decode_buf[16];
    // Input length % 4 == 1 should be rejected
    assert(!Base64UrlDecode("A", decode_buf, 16));
    assert(!Base64UrlDecode("abcde", decode_buf, 16));

    // Input with non-zero leftover/padding bits should be rejected
    assert(Base64UrlDecode("AAAAAAAAAAAAAAAAAAAAAA", decode_buf, 16));
    assert(!Base64UrlDecode("AAAAAAAAAAAAAAAAAAAAAB", decode_buf, 16));

    // Test DecodeBase255 validation
    // Empty input should be rejected
    assert(!DecodeBase255("").has_value());
    // Invalid character (less than \x01) should be rejected
    assert(!DecodeBase255(std::string("\x00", 1)).has_value());
    // Safe decode of valid base-255 string
    auto val1 = DecodeBase255("\x01");
    assert(val1.has_value() && *val1 == 0);
    auto val2 = DecodeBase255("\x02\x02"); // 1*255 + 1 = 256
    assert(val2.has_value() && *val2 == 256);

    // Overflow check
    std::string max_encoded = EncodeBase255(std::numeric_limits<uint64_t>::max());
    assert(DecodeBase255(max_encoded).has_value());
    
    std::string append_overflow = max_encoded + "\x02";
    assert(!DecodeBase255(append_overflow).has_value());

    // Test NamespacePrefix::Parse validation
    std::string_view parsed_ns;
    uint64_t parsed_epoch;
    std::string_view parsed_user_key;

    // Correct prefix parsing
    std::string valid_key = std::string("\x02\x00\x02\x00mykey", 9);
    std::cout << "valid_key size: " << valid_key.size() << std::endl;
    for (size_t i = 0; i < valid_key.size(); ++i) {
        std::cout << "  char[" << i << "] = " << (int)(unsigned char)valid_key[i] << std::endl;
    }
    bool parsed = NamespacePrefix::Parse(valid_key, parsed_ns, parsed_epoch, parsed_user_key);
    std::cout << "parsed: " << parsed << std::endl;
    if (parsed) {
        std::cout << "parsed_ns: size=" << parsed_ns.size() << " val=" << (int)(unsigned char)parsed_ns[0] << std::endl;
        std::cout << "parsed_epoch: " << parsed_epoch << std::endl;
        std::cout << "parsed_user_key: size=" << parsed_user_key.size() << " val='" << parsed_user_key << "'" << std::endl;
    }
    assert(parsed);
    assert(parsed_ns == "\x02");
    assert(parsed_epoch == 1);
    assert(parsed_user_key == "mykey");

    // Empty namespace segment (delim1 == ns_start)
    std::string empty_ns_key = std::string("\x00\x02\x00mykey", 8);
    assert(!NamespacePrefix::Parse(empty_ns_key, parsed_ns, parsed_epoch, parsed_user_key));

    // Empty epoch segment (delim2 == epoch_start)
    std::string empty_epoch_key = std::string("\x02\x00\x00mykey", 8);
    assert(!NamespacePrefix::Parse(empty_epoch_key, parsed_ns, parsed_epoch, parsed_user_key));

    // Invalid epoch decode (overflow epoch segment)
    std::string overflow_epoch_key = std::string("\x02\x00", 2) + append_overflow + std::string("\x00mykey", 6);
    assert(!NamespacePrefix::Parse(overflow_epoch_key, parsed_ns, parsed_epoch, parsed_user_key));

    std::cout << "TestBase64AndBase255 passed!" << std::endl;
}

void TestNamespacePrefixing()
{
    std::cout << "Running TestNamespacePrefixing..." << std::endl;
    std::string original_ns = EloqKV::current_namespace;

    // Default namespace key is prefixless
    EloqKV::current_namespace = "default";
    std::string isolated_default_key = ApplyNamespace("mykey");
    assert(isolated_default_key == "mykey");

    std::string isolated_default_next = ComposeNamespaceKeyNext("default");
    assert(isolated_default_next == "");

    // Custom namespace with prefix: encoded_ns_id + \x00 + encoded_epoch + \x00
    // e.g. ID = 1 (\x02), Epoch = 1 (\x02) => "\x02\x00\x02\x00"
    std::string ns_id_v1 = std::string("\x02\x00\x02\x00", 4);
    EloqKV::current_namespace = ns_id_v1;
    std::string isolated_custom_key = ApplyNamespace("mykey");
    assert(isolated_custom_key.size() == ns_id_v1.size() + 5);
    assert(isolated_custom_key[0] == '\x02');
    assert(isolated_custom_key[1] == '\x00');
    assert(isolated_custom_key[2] == '\x02');
    assert(isolated_custom_key[3] == '\x00');
    assert(isolated_custom_key.substr(4) == "mykey");

    // Empty namespace remains un-prefixed (used for system metadata lookup)
    EloqKV::current_namespace = "";
    std::string isolated_empty_key = ApplyNamespace("mykey");
    assert(isolated_empty_key == "mykey");

    // Test isolated ComposeNamespaceKeyNext for custom namespace
    std::string isolated_custom_next = ComposeNamespaceKeyNext(ns_id_v1);
    assert(isolated_custom_next.size() == ns_id_v1.size());
    assert(isolated_custom_next[0] == '\x02');
    assert(isolated_custom_next[1] == '\x00');
    assert(isolated_custom_next[2] == '\x02');
    assert(isolated_custom_next[3] == '\x01');

    // Test isolated ComposeNamespaceKeyNext for multi-byte encoded ID
    std::string ns_id_multibyte = std::string("\x01\x02\x00\x02\x00", 5);
    std::string isolated_multibyte_next = ComposeNamespaceKeyNext(ns_id_multibyte);
    assert(isolated_multibyte_next.size() == ns_id_multibyte.size());
    assert(isolated_multibyte_next[0] == '\x01');
    assert(isolated_multibyte_next[1] == '\x02');
    assert(isolated_multibyte_next[2] == '\x00');
    assert(isolated_multibyte_next[3] == '\x02');
    assert(isolated_multibyte_next[4] == '\x01');

    // Test all-0xFF overflow case in ComposeNamespaceKeyNext
    std::string all_ff = std::string("\xFF\xFF", 2);
    std::string all_ff_next = ComposeNamespaceKeyNext(all_ff);
    assert(all_ff_next == "");

    EloqKV::current_namespace = original_ns;
    std::cout << "TestNamespacePrefixing passed!" << std::endl;
}

int main()
{
    TestBase64AndBase255();
    TestNamespacePrefixing();
    std::cout << "All namespace unit tests passed successfully!" << std::endl;
    return 0;
}
