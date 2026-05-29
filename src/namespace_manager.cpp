#include "namespace_manager.h"
#include "b255_encode.h"
#include <mutex>
#include <memory>

namespace EloqKV
{

// ==========================================
// MemoryNamespaceStorage Implementation
// ==========================================

bool MemoryNamespaceStorage::Add(std::string_view ns, std::string_view token)
{
    return rcu_state_.Update([&](StorageState& new_state) -> bool {
        auto it_ns = new_state.ns_to_token.find(ns);
        if (it_ns != new_state.ns_to_token.end())
        {
            return it_ns->second == token;
        }
        if (new_state.token_to_ns.find(token) != new_state.token_to_ns.end())
        {
            return false;
        }
        new_state.token_to_ns.emplace(token, ns);
        new_state.ns_to_token.emplace(ns, token);
        uint64_t id = ++new_state.next_id;
        new_state.ns_to_id.emplace(ns, EncodeBase255(id) + std::string{B255_DELIMITER});
        return true;
    });
}

bool MemoryNamespaceStorage::Set(std::string_view ns, std::string_view token)
{
    return rcu_state_.Update([&](StorageState& new_state) -> bool {
        auto it_token = new_state.token_to_ns.find(token);
        if (it_token != new_state.token_to_ns.end() && it_token->second != ns)
        {
            return false;
        }

        auto it_ns = new_state.ns_to_token.find(ns);
        if (it_ns != new_state.ns_to_token.end())
        {
            if (it_ns->second == token)
            {
                return true;
            }
            new_state.token_to_ns.erase(it_ns->second);
            new_state.ns_to_token[std::string(ns)] = token;
            new_state.token_to_ns.emplace(token, ns);
            return true;
        }

        new_state.token_to_ns.emplace(token, ns);
        new_state.ns_to_token.emplace(ns, token);
        uint64_t id = ++new_state.next_id;
        new_state.ns_to_id.emplace(ns, EncodeBase255(id) + std::string{B255_DELIMITER});
        return true;
    });
}

bool MemoryNamespaceStorage::Del(std::string_view ns)
{
    return rcu_state_.Update([&](StorageState& new_state) -> bool {
        auto it_ns = new_state.ns_to_token.find(ns);
        if (it_ns != new_state.ns_to_token.end())
        {
            new_state.token_to_ns.erase(it_ns->second);
            new_state.ns_to_id.erase(std::string(ns));
            new_state.ns_to_token.erase(std::string(ns));
            return true;
        }
        return false;
    });
}

std::string MemoryNamespaceStorage::GetToken(std::string_view ns)
{
    auto current_state = rcu_state_.Read();
    if (current_state)
    {
        auto it = current_state->ns_to_token.find(ns);
        if (it != current_state->ns_to_token.end())
        {
            return it->second;
        }
    }
    return "";
}

std::string MemoryNamespaceStorage::GetNamespaceFromToken(std::string_view token, std::string &ns_id, uint64_t &epoch)
{
    ns_id = "";
    epoch = 1;
    auto current_state = rcu_state_.Read();
    if (current_state)
    {
        auto it = current_state->token_to_ns.find(token);
        if (it != current_state->token_to_ns.end())
        {
            auto it_id = current_state->ns_to_id.find(it->second);
            if (it_id != current_state->ns_to_id.end())
            {
                ns_id = it_id->second;
            }
            return it->second;
        }
    }
    return "";
}

std::map<std::string, std::string, std::less<>> MemoryNamespaceStorage::List()
{
    auto current_state = rcu_state_.Read();
    if (current_state)
    {
        return current_state->token_to_ns;
    }
    return {};
}

// ==========================================
// NamespaceManager Implementation
// ==========================================

NamespaceManager::NamespaceManager(std::unique_ptr<INamespaceStorage> storage)
    : storage_(std::move(storage))
{
}

bool NamespaceManager::Add(std::string_view ns, std::string_view token)
{
    if (storage_)
    {
        return storage_->Add(ns, token);
    }
    return false;
}

bool NamespaceManager::Set(std::string_view ns, std::string_view token)
{
    if (storage_)
    {
        bool ok = storage_->Set(ns, token);
        if (ok)
        {
            RemoveMetadata(ns);
        }
        return ok;
    }
    return false;
}

bool NamespaceManager::Del(std::string_view ns)
{
    if (storage_)
    {
        bool ok = storage_->Del(ns);
        if (ok)
        {
            RemoveMetadata(ns);
        }
        return ok;
    }
    return false;
}

std::string NamespaceManager::Get(std::string_view ns) const
{
    if (storage_)
    {
        return storage_->GetToken(ns);
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
    if (storage_)
    {
        uint64_t dummy_epoch = 1;
        return storage_->GetNamespaceFromToken(token, ns_id, dummy_epoch);
    }
    ns_id = "";
    return "";
}

std::map<std::string, std::string, std::less<>> NamespaceManager::List() const
{
    if (storage_)
    {
        return storage_->List();
    }
    return {};
}

std::shared_ptr<NamespaceMetadata> NamespaceManager::GetMetadataByToken(std::string_view token) const
{
    auto current_state = rcu_state_.Read();
    if (current_state)
    {
        auto it = current_state->token_metadata.find(std::string(token));
        if (it != current_state->token_metadata.end())
        {
            return it->second;
        }
    }

    if (storage_)
    {
        std::string ns_id;
        uint64_t epoch = 1;
        std::string ns_name = storage_->GetNamespaceFromToken(token, ns_id, epoch);
        if (ns_name.empty())
        {
            return nullptr;
        }

        return rcu_state_.Update([&](CacheState& new_state) -> std::shared_ptr<NamespaceMetadata> {
            auto it = new_state.token_metadata.find(std::string(token));
            if (it != new_state.token_metadata.end())
            {
                return it->second;
            }

            auto meta = std::make_shared<NamespaceMetadata>();
            meta->ns_name = ns_name;
            meta->encoded_id = ns_id;
            meta->epoch.store(epoch);

            new_state.token_metadata[std::string(token)] = meta;
            new_state.ns_metadata[ns_name] = meta;
            return meta;
        });
    }

    return nullptr;
}

std::shared_ptr<NamespaceMetadata> NamespaceManager::GetOrCreateMetadata(std::string_view ns_name, std::string_view encoded_id, uint64_t epoch)
{
    return rcu_state_.Update([&](CacheState& new_state) -> std::shared_ptr<NamespaceMetadata> {
        auto it = new_state.ns_metadata.find(std::string(ns_name));
        if (it != new_state.ns_metadata.end())
        {
            it->second->epoch.store(epoch);
            return it->second;
        }

        auto meta = std::make_shared<NamespaceMetadata>();
        meta->ns_name = ns_name;
        meta->encoded_id = encoded_id;
        meta->epoch.store(epoch);

        new_state.ns_metadata[std::string(ns_name)] = meta;
        return meta;
    });
}

void NamespaceManager::RemoveMetadata(std::string_view ns_name)
{
    rcu_state_.Update([&](CacheState& new_state) {
        auto it = new_state.ns_metadata.find(std::string(ns_name));
        if (it != new_state.ns_metadata.end())
        {
            for (auto token_it = new_state.token_metadata.begin(); token_it != new_state.token_metadata.end(); )
            {
                if (token_it->second == it->second)
                {
                    token_it = new_state.token_metadata.erase(token_it);
                }
                else
                {
                    ++token_it;
                }
            }
            new_state.ns_metadata.erase(std::string(ns_name));
        }
    });
}

} // namespace EloqKV
