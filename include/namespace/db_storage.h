#pragma once

#include "manager.h"
#include <map>
#include <string>
#include <string_view>

namespace EloqKV
{

class RedisServiceImpl;

class DbNamespaceStorage : public INamespaceStorage
{
public:
    explicit DbNamespaceStorage(RedisServiceImpl *server) : server_(server) {}
    ~DbNamespaceStorage() override = default;

    NamespaceToken GetToken(std::string_view ns) override;
    std::string GetNamespaceFromToken(const NamespaceToken &token,
                                      std::string &ns_id,
                                      uint64_t &epoch) override;
    bool Add(std::string_view ns, const NamespaceToken &token) override;
    bool Set(std::string_view ns, const NamespaceToken &token) override;
    bool Del(std::string_view ns) override;
    std::map<NamespaceToken, std::string> List() override;

private:
    RedisServiceImpl *server_;
};

}  // namespace EloqKV
