#pragma once

#include <string>
#include <string_view>
#include <map>
#include <shared_mutex>

namespace EloqKV
{
class RedisServiceImpl;

class NamespaceManager
{
public:
    NamespaceManager() = default;
    explicit NamespaceManager(RedisServiceImpl *server) : server_(server) {}
    ~NamespaceManager() = default;

    NamespaceManager(const NamespaceManager&) = delete;
    NamespaceManager& operator=(const NamespaceManager&) = delete;
    NamespaceManager(NamespaceManager&&) = delete;
    NamespaceManager& operator=(NamespaceManager&&) = delete;

    bool Add(std::string_view ns, std::string_view token);
    bool Set(std::string_view ns, std::string_view token);
    bool Del(std::string_view ns);
    std::string Get(std::string_view ns) const;
    std::string GetByToken(std::string_view token) const;
    std::string GetByToken(std::string_view token, std::string &ns_id) const;
    std::map<std::string, std::string, std::less<>> List() const;

private:
    RedisServiceImpl *server_{nullptr};
    mutable std::shared_mutex mu_;
    std::map<std::string, std::string, std::less<>> token_to_ns_;
    std::map<std::string, std::string, std::less<>> ns_to_token_;
    std::map<std::string, std::string, std::less<>> ns_to_id_;
};

} // namespace EloqKV
