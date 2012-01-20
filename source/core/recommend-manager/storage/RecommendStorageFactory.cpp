#include "RecommendStorageFactory.h"
#include "LocalUserManager.h"
#include "RemoteUserManager.h"
#include "LocalPurchaseManager.h"
#include "RemotePurchaseManager.h"
#include <configuration-manager/CassandraStorageConfig.h>

namespace bfs = boost::filesystem;

namespace sf1r
{

RecommendStorageFactory::RecommendStorageFactory(
    const std::string& collection,
    const std::string& dataDir,
    const CassandraStorageConfig& cassandraConfig
)
    : collection_(collection)
    , userDir_(bfs::path(dataDir) / "user")
    , eventDir_(bfs::path(dataDir) / "event")
    , cassandraConfig_(cassandraConfig)
{
    if (! cassandraConfig_.enable)
    {
        bfs::create_directory(userDir_);
        bfs::create_directory(eventDir_);
    }
}

UserManager* RecommendStorageFactory::createUserManager() const
{
    if (cassandraConfig_.enable)
    {
        return new RemoteUserManager(cassandraConfig_.keyspace, collection_);
    }
    else
    {
        return new LocalUserManager((userDir_ / "user.db").string());
    }
}

PurchaseManager* RecommendStorageFactory::createPurchaseManager() const
{
    if (cassandraConfig_.enable)
    {
        return new RemotePurchaseManager(cassandraConfig_.keyspace, collection_);
    }
    else
    {
        return new LocalPurchaseManager((eventDir_ / "purchase.db").string());
    }
}

} // namespace sf1r
