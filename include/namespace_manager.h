#pragma once

#include <string>
#include <string_view>
#include <map>
#include <shared_mutex>
#include <memory>
#include <unordered_map>
#include <atomic>

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
    virtual std::string GetNamespaceFromToken(std::string_view token, std::string &ns_id, uint64_t &epoch) = 0;
    virtual bool Add(std::string_view ns, std::string_view token) = 0;
    virtual bool Set(std::string_view ns, std::string_view token) = 0;
    virtual bool Del(std::string_view ns) = 0;
    virtual std::map<std::string, std::string, std::less<>> List() = 0;
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
    std::string GetNamespaceFromToken(std::string_view token, std::string &ns_id, uint64_t &epoch) override;
    std::map<std::string, std::string, std::less<>> List() override;

private:
    mutable std::shared_mutex mu_;
    std::map<std::string, std::string, std::less<>> token_to_ns_;
    std::map<std::string, std::string, std::less<>> ns_to_token_;
    std::map<std::string, std::string, std::less<>> ns_to_id_;
    uint64_t next_id_{2};
};

class NamespaceManager
{
public:
    NamespaceManager();
    explicit NamespaceManager(std::unique_ptr<INamespaceStorage> storage);
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

    std::shared_ptr<NamespaceMetadata> GetMetadataByToken(std::string_view token) const;
    std::shared_ptr<NamespaceMetadata> GetOrCreateMetadata(std::string_view ns_name, std::string_view encoded_id, uint64_t epoch);
    void RemoveMetadata(std::string_view ns_name);

private:
    std::unique_ptr<INamespaceStorage> storage_;
    mutable std::shared_mutex meta_mu_;
    mutable std::unordered_map<std::string, std::shared_ptr<NamespaceMetadata>> ns_metadata_;
    mutable std::unordered_map<std::string, std::shared_ptr<NamespaceMetadata>> token_metadata_;
};

} // namespace EloqKV

