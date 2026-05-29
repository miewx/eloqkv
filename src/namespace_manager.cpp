#include "namespace_manager.h"
#include "b255_encode.h"
#include <mutex>

namespace EloqKV
{

// ==========================================
// MemoryNamespaceStorage Implementation
// ==========================================

bool MemoryNamespaceStorage::Add(std::string_view ns, std::string_view token)
{
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
    uint64_t id = ++next_id_;
    ns_to_id_.emplace(ns, EncodeBase255(id) + std::string{B255_DELIMITER});
    return true;
}

bool MemoryNamespaceStorage::Set(std::string_view ns, std::string_view token)
{
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
    uint64_t id = ++next_id_;
    ns_to_id_.emplace(ns, EncodeBase255(id) + std::string{B255_DELIMITER});
    return true;
}

bool MemoryNamespaceStorage::Del(std::string_view ns)
{
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

std::string MemoryNamespaceStorage::GetToken(std::string_view ns)
{
    std::shared_lock<std::shared_mutex> lock(mu_);
    auto it = ns_to_token_.find(ns);
    if (it != ns_to_token_.end())
    {
        return it->second;
    }
    return "";
}

std::string MemoryNamespaceStorage::GetNamespaceFromToken(std::string_view token, std::string &ns_id, uint64_t &epoch)
{
    ns_id = "";
    epoch = 1;
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

std::map<std::string, std::string, std::less<>> MemoryNamespaceStorage::List()
{
    std::shared_lock<std::shared_mutex> lock(mu_);
    return token_to_ns_;
}

// ==========================================
// NamespaceManager Implementation
// ==========================================

NamespaceManager::NamespaceManager()
    : storage_(std::make_unique<MemoryNamespaceStorage>())
{
}

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
    {
        std::shared_lock<std::shared_mutex> lock(meta_mu_);
        auto it = token_metadata_.find(std::string(token));
        if (it != token_metadata_.end())
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

        std::unique_lock<std::shared_mutex> lock(meta_mu_);
        // Double check
        auto it = token_metadata_.find(std::string(token));
        if (it != token_metadata_.end())
        {
            return it->second;
        }

        auto meta = std::make_shared<NamespaceMetadata>();
        meta->ns_name = ns_name;
        meta->encoded_id = ns_id;
        meta->epoch.store(epoch);

        token_metadata_[std::string(token)] = meta;
        ns_metadata_[ns_name] = meta;
        return meta;
    }

    return nullptr;
}

std::shared_ptr<NamespaceMetadata> NamespaceManager::GetOrCreateMetadata(std::string_view ns_name, std::string_view encoded_id, uint64_t epoch)
{
    std::unique_lock<std::shared_mutex> lock(meta_mu_);
    auto it = ns_metadata_.find(std::string(ns_name));
    if (it != ns_metadata_.end())
    {
        it->second->epoch.store(epoch);
        return it->second;
    }

    auto meta = std::make_shared<NamespaceMetadata>();
    meta->ns_name = ns_name;
    meta->encoded_id = encoded_id;
    meta->epoch.store(epoch);

    ns_metadata_[std::string(ns_name)] = meta;
    return meta;
}

void NamespaceManager::RemoveMetadata(std::string_view ns_name)
{
    std::unique_lock<std::shared_mutex> lock(meta_mu_);
    auto it = ns_metadata_.find(std::string(ns_name));
    if (it != ns_metadata_.end())
    {
        for (auto token_it = token_metadata_.begin(); token_it != token_metadata_.end(); )
        {
            if (token_it->second == it->second)
            {
                token_it = token_metadata_.erase(token_it);
            }
            else
            {
                ++token_it;
            }
        }
        ns_metadata_.erase(it);
    }
}

} // namespace EloqKV

