#include "namespace/db_storage.h"
#include "redis_service.h"

namespace EloqKV
{

NamespaceToken DbNamespaceStorage::GetToken(std::string_view ns)
{
    return server_->GetNamespaceTokenFromDB(ns);
}

std::string DbNamespaceStorage::GetNamespaceFromToken(const NamespaceToken &token,
                                                      std::string &ns_id,
                                                      uint64_t &epoch)
{
    return server_->GetNamespaceFromTokenFromDB(token, ns_id, epoch);
}

bool DbNamespaceStorage::Add(std::string_view ns, const NamespaceToken &token)
{
    return server_->AddNamespaceToDB(ns, token);
}

bool DbNamespaceStorage::Set(std::string_view ns, const NamespaceToken &token)
{
    return server_->SetNamespaceInDB(ns, token);
}

bool DbNamespaceStorage::Del(std::string_view ns)
{
    return server_->DelNamespaceFromDB(ns);
}

std::map<NamespaceToken, std::string> DbNamespaceStorage::List()
{
    return server_->ListNamespacesFromDB();
}

}  // namespace EloqKV
