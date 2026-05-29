#include <iostream>
#include <cassert>
#include "eloqkv_key.h"

using namespace EloqKV;

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
    TestNamespacePrefixing();
    std::cout << "All namespace unit tests passed successfully!" << std::endl;
    return 0;
}
