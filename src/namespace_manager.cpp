#include "namespace_manager.h"
#include "b255_encode.h"
#include <mutex>
#ifndef UNIT_TEST
#include "redis_service.h"
#else
#include <map>
namespace EloqKV {
class RedisServiceImpl {
public:
    bool AddNamespaceToDB(std::string_view ns, std::string_view token) { return false; }
    bool SetNamespaceInDB(std::string_view ns, std::string_view token) { return false; }
    bool DelNamespaceFromDB(std::string_view ns) { return false; }
    std::string GetNamespaceTokenFromDB(std::string_view ns) { return ""; }
    std::string GetNamespaceFromTokenFromDB(std::string_view token, std::string &ns_id) { return ""; }
    std::map<std::string, std::string, std::less<>> ListNamespacesFromDB() { return {}; }
};
}
#endif

namespace EloqKV
{

bool NamespaceManager::Add(std::string_view ns, std::string_view token)
{
    if (server_)
    {
        return server_->AddNamespaceToDB(ns, token);
    }

    std::unique_lock<std::shared_mutex> lock(mu_);
    auto it_ns = ns_to_token_.find(ns);
    if (it_ns != ns_to_token_.end())
    {
        return it_ns->second == token;
    }
    if (token_to_ns_.find(token) != token_to_ns_.end())
    {
        return false;
    }
    token_to_ns_.emplace(token, ns);
    ns_to_token_.emplace(ns, token);
    uint64_t id = 2 + ns_to_id_.size();
    if (ns == "default")
    {
        ns_to_id_.emplace(ns, std::string(1, '\x01') + std::string(1, B255_DELIMITER));
    }
    else
    {
        ns_to_id_.emplace(ns, EncodeBase255(id) + std::string(1, B255_DELIMITER));
    }
    return true;
}

bool NamespaceManager::Set(std::string_view ns, std::string_view token)
{
    if (server_)
    {
        return server_->SetNamespaceInDB(ns, token);
    }

    std::unique_lock<std::shared_mutex> lock(mu_);
    auto it_token = token_to_ns_.find(token);
    if (it_token != token_to_ns_.end() && it_token->second != ns)
    {
        return false;
    }

    auto it_ns = ns_to_token_.find(ns);
    if (it_ns != ns_to_token_.end())
    {
        if (it_ns->second == token)
        {
            return true;
        }
        token_to_ns_.erase(it_ns->second);
        it_ns->second = token;
        token_to_ns_.emplace(token, ns);
        return true;
    }

    token_to_ns_.emplace(token, ns);
    ns_to_token_.emplace(ns, token);
    uint64_t id = 2 + ns_to_id_.size();
    if (ns == "default")
    {
        ns_to_id_.emplace(ns, std::string(1, '\x01') + std::string(1, B255_DELIMITER));
    }
    else
    {
        ns_to_id_.emplace(ns, EncodeBase255(id) + std::string(1, B255_DELIMITER));
    }
    return true;
}

bool NamespaceManager::Del(std::string_view ns)
{
    if (server_)
    {
        return server_->DelNamespaceFromDB(ns);
    }

    std::unique_lock<std::shared_mutex> lock(mu_);
    auto it_ns = ns_to_token_.find(ns);
    if (it_ns != ns_to_token_.end())
    {
        token_to_ns_.erase(it_ns->second);
        ns_to_id_.erase(std::string(ns));
        ns_to_token_.erase(it_ns);
        return true;
    }
    return false;
}

std::string NamespaceManager::Get(std::string_view ns) const
{
    if (server_)
    {
        return server_->GetNamespaceTokenFromDB(ns);
    }

    std::shared_lock<std::shared_mutex> lock(mu_);
    auto it = ns_to_token_.find(ns);
    if (it != ns_to_token_.end())
    {
        return it->second;
    }
    return "";
}

std::string NamespaceManager::GetByToken(std::string_view token) const
{
    std::string ns_id;
    return GetByToken(token, ns_id);
}

std::string NamespaceManager::GetByToken(std::string_view token, std::string &ns_id) const
{
    if (server_)
    {
        return server_->GetNamespaceFromTokenFromDB(token, ns_id);
    }

    ns_id = "";
    std::shared_lock<std::shared_mutex> lock(mu_);
    auto it = token_to_ns_.find(token);
    if (it != token_to_ns_.end())
    {
        auto it_id = ns_to_id_.find(it->second);
        if (it_id != ns_to_id_.end())
        {
            ns_id = it_id->second;
        }
        return it->second;
    }
    return "";
}

std::map<std::string, std::string, std::less<>> NamespaceManager::List() const
{
    if (server_)
    {
        return server_->ListNamespacesFromDB();
    }

    std::shared_lock<std::shared_mutex> lock(mu_);
    return token_to_ns_;
}

} // namespace EloqKV
