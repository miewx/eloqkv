#include <iostream>
#include <cassert>
#include <fstream>
#include <vector>
#include <algorithm>
#include "namespace_manager.h"
#include "INIReader.h"
#include "eloqkv_key.h"

using namespace EloqKV;



void TestNamespaceManager()
{
    std::cout << "Running TestNamespaceManager..." << std::endl;
    EloqKV::NamespaceManager mgr;

    // Test Add
    assert(mgr.Add("ns1", "token1") == true);
    assert(mgr.Add("ns1", "token2") == false); // Duplicate ns
    assert(mgr.Add("ns2", "token1") == false); // Duplicate token
    assert(mgr.Add("ns2", "token2") == true);

    // Test GetByToken / Get
    assert(mgr.GetByToken("token1") == "ns1");
    assert(mgr.GetByToken("token2") == "ns2");
    assert(mgr.Get("ns1") == "token1");
    assert(mgr.Get("ns2") == "token2");

    // Test Set
    assert(mgr.Set("ns1", "token3") == true); // Update token for ns1
    assert(mgr.Get("ns1") == "token3");
    assert(mgr.GetByToken("token3") == "ns1");
    assert(mgr.GetByToken("token1") == ""); // Old token should be deleted

    // Test List
    auto list = mgr.List();
    assert(list.size() == 2);
    assert(list["token3"] == "ns1");
    assert(list["token2"] == "ns2");

    // Test Del
    assert(mgr.Del("ns1") == true);
    assert(mgr.Del("ns1") == false); // Deleted already
    assert(mgr.Get("ns1") == "");
    assert(mgr.GetByToken("token3") == "");

    std::cout << "TestNamespaceManager passed!" << std::endl;
}

void TestNamespacePrefixing()
{
    std::cout << "Running TestNamespacePrefixing..." << std::endl;

    // --- CASE 1: Namespace Disabled (enable_namespace = false) ---
    EloqKV::enable_namespace = false;

    // Custom namespace has no prefixing when disabled
    EloqKV::current_namespace = "ns1";
    std::string disabled_custom_key = ApplyNamespace("mykey");
    assert(disabled_custom_key == "mykey");

    // Default namespace has no prefixing when disabled
    EloqKV::current_namespace = "default";
    std::string disabled_default_key = ApplyNamespace("mykey");
    assert(disabled_default_key == "mykey");

    // Empty namespace has no prefixing
    EloqKV::current_namespace = "";
    std::string disabled_empty_key = ApplyNamespace("mykey");
    assert(disabled_empty_key == "mykey");

    // ComposeNamespaceKeyNext returns empty string when disabled
    std::string disabled_next = ComposeNamespaceKeyNext("ns1");
    assert(disabled_next == "");


    // --- CASE 2: Absolutely Isolated Mode (enable_namespace = true) ---
    EloqKV::enable_namespace = true;

    // Default namespace key is prefixed with \x01\x00 (EncodeBase255(0) + \x00)
    EloqKV::current_namespace = "default";
    std::string isolated_default_key = ApplyNamespace("mykey");
    assert(isolated_default_key.size() == 2 + 5);
    assert(isolated_default_key[0] == '\x01');
    assert(isolated_default_key[1] == '\x00');
    assert(isolated_default_key.substr(2) == "mykey");

    // Custom namespace with prefix "\x02\x00" (ID 1 + \x00)
    EloqKV::current_namespace = std::string("\x02\x00", 2);
    std::string isolated_custom_key = ApplyNamespace("mykey");
    assert(isolated_custom_key.size() == 2 + 5);
    assert(isolated_custom_key[0] == '\x02');
    assert(isolated_custom_key[1] == '\x00');
    assert(isolated_custom_key.substr(2) == "mykey");

    // Empty namespace remains un-prefixed (used for system metadata lookup)
    EloqKV::current_namespace = "";
    std::string isolated_empty_key = ApplyNamespace("mykey");
    assert(isolated_empty_key == "mykey");

    // Test isolated ComposeNamespaceKeyNext for default namespace
    std::string isolated_default_next = ComposeNamespaceKeyNext("default");
    assert(isolated_default_next.size() == 2);
    assert(isolated_default_next[0] == '\x01');
    assert(isolated_default_next[1] == '\x01');

    // Test isolated ComposeNamespaceKeyNext for custom namespace
    std::string isolated_custom_next = ComposeNamespaceKeyNext(std::string("\x02\x00", 2));
    assert(isolated_custom_next.size() == 2);
    assert(isolated_custom_next[0] == '\x02');
    assert(isolated_custom_next[1] == '\x01');

    // Test isolated ComposeNamespaceKeyNext for multi-byte encoded ID
    std::string isolated_multibyte_next = ComposeNamespaceKeyNext(std::string("\x01\x02\x00", 3));
    assert(isolated_multibyte_next.size() == 3);
    assert(isolated_multibyte_next[0] == '\x01');
    assert(isolated_multibyte_next[1] == '\x02');
    assert(isolated_multibyte_next[2] == '\x01');

    std::cout << "TestNamespacePrefixing passed!" << std::endl;
}

int main()
{
    TestNamespaceManager();
    TestNamespacePrefixing();
    std::cout << "All namespace unit tests passed successfully!" << std::endl;
    return 0;
}
