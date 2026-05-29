#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace EloqKV
{

template <typename T>
class RcuWrapper
{
public:
    RcuWrapper() : state_(std::make_shared<T>())
    {
    }
    explicit RcuWrapper(std::shared_ptr<const T> state)
        : state_(std::move(state))
    {
    }

    std::shared_ptr<const T> Read() const
    {
        return std::atomic_load(&state_);
    }

    template <typename Func>
    auto Update(Func &&func) -> decltype(func(std::declval<T &>()))
    {
        std::lock_guard<std::mutex> lock(write_mu_);
        auto latest = std::atomic_load(&state_);
        auto copy = std::make_shared<T>(*latest);
        if constexpr (std::is_void_v<decltype(func(std::declval<T &>()))>)
        {
            func(*copy);
            std::atomic_store(&state_, std::shared_ptr<const T>(copy));
        }
        else
        {
            auto res = func(*copy);
            std::atomic_store(&state_, std::shared_ptr<const T>(copy));
            return res;
        }
    }

private:
    std::shared_ptr<const T> state_;
    mutable std::mutex write_mu_;
};

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
    RcuWrapper<StorageState> rcu_state_;
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
    mutable RcuWrapper<CacheState> rcu_state_;
};

}  // namespace EloqKV
