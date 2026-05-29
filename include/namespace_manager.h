#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include "rcu.h"

namespace EloqKV
{

struct NamespaceMetadata
{
    std::string ns_name;
    std::string encoded_id;
    std::atomic<uint64_t> epoch{1};
};

class INamespaceStorage
{
public:
    virtual ~INamespaceStorage() = default;
    virtual std::string GetToken(std::string_view ns) = 0;
    virtual std::string GetNamespaceFromToken(std::string_view token,
                                              std::string &ns_id,
                                              uint64_t &epoch) = 0;
    virtual bool Add(std::string_view ns, std::string_view token) = 0;
    virtual bool Set(std::string_view ns, std::string_view token) = 0;
    virtual bool Del(std::string_view ns) = 0;
    virtual std::map<std::string, std::string, std::less<>> List() = 0;
};

struct StorageState
{
    std::map<std::string, std::string, std::less<>> token_to_ns;
    std::map<std::string, std::string, std::less<>> ns_to_token;
    std::map<std::string, std::string, std::less<>> ns_to_id;
    uint64_t next_id{2};
};

class MemoryNamespaceStorage : public INamespaceStorage
{
public:
    MemoryNamespaceStorage() = default;
    ~MemoryNamespaceStorage() override = default;

    bool Add(std::string_view ns, std::string_view token) override;
    bool Set(std::string_view ns, std::string_view token) override;
    bool Del(std::string_view ns) override;
    std::string GetToken(std::string_view ns) override;
    std::string GetNamespaceFromToken(std::string_view token,
                                      std::string &ns_id,
                                      uint64_t &epoch) override;
    std::map<std::string, std::string, std::less<>> List() override;

private:
    Rcu<StorageState> rcu_state_;
};

struct CacheState
{
    std::unordered_map<std::string, std::shared_ptr<NamespaceMetadata>>
        ns_metadata;
    std::unordered_map<std::string, std::shared_ptr<NamespaceMetadata>>
        token_metadata;
};

class NamespaceManager
{
public:
    NamespaceManager() = default;
    explicit NamespaceManager(std::unique_ptr<INamespaceStorage> storage);
    ~NamespaceManager() = default;

    NamespaceManager(const NamespaceManager &) = delete;
    NamespaceManager &operator=(const NamespaceManager &) = delete;
    NamespaceManager(NamespaceManager &&) = delete;
    NamespaceManager &operator=(NamespaceManager &&) = delete;

    bool Add(std::string_view ns, std::string_view token);
    bool Set(std::string_view ns, std::string_view token);
    bool Del(std::string_view ns);
    std::string Get(std::string_view ns) const;
    std::string GetByToken(std::string_view token) const;
    std::string GetByToken(std::string_view token, std::string &ns_id) const;
    std::map<std::string, std::string, std::less<>> List() const;

    std::shared_ptr<NamespaceMetadata> GetMetadataByToken(
        std::string_view token) const;
    std::shared_ptr<NamespaceMetadata> GetOrCreateMetadata(
        std::string_view ns_name, std::string_view encoded_id, uint64_t epoch);
    void RemoveMetadata(std::string_view ns_name);

private:
    std::unique_ptr<INamespaceStorage> storage_;
    mutable Rcu<CacheState> rcu_state_;
};

}  // namespace EloqKV
