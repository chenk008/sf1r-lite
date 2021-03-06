/**
 * @file MasterManagerBase.h
 * @author Zhongxia Li
 * @date Sep 20, 2011S
 * @brief Management (Coordination) for Master node using ZooKeeper.
 */
#ifndef MASTER_MANAGER_BASE_H_
#define MASTER_MANAGER_BASE_H_

#include "ZooKeeperNamespace.h"
#include "ZooKeeperManager.h"

#include "IDistributeService.h"
#include <map>
#include <vector>
#include <queue>
#include <sstream>

#include <util/singleton.h>
#include <net/aggregator/AggregatorConfig.h>
#include <net/aggregator/Aggregator.h>

#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>

using namespace net::aggregator;

namespace sf1r
{

class MasterManagerBase : public ZooKeeperEventHandler
{
public:
    enum MasterStateType
    {
        MASTER_STATE_INIT,
        MASTER_STATE_STARTING,
        MASTER_STATE_STARTING_WAIT_ZOOKEEPER,
        MASTER_STATE_STARTING_WAIT_WORKERS,
        MASTER_STATE_STARTED,
        //MASTER_STATE_FAILOVERING,
        //MASTER_STATE_RECOVERING,
        //MASTER_STATE_WAIT_WOKER_FINISH_REQ,
    };

    typedef std::map<shardid_t, boost::shared_ptr<Sf1rNode> > WorkerMapT;
    typedef std::map<shardid_t, std::map<replicaid_t, boost::shared_ptr<Sf1rNode> > > ROWorkerMapT;
    typedef boost::function<bool()>  EventCBType;

public:
    static MasterManagerBase* get()
    {
        return izenelib::util::Singleton<MasterManagerBase>::get();
    }

    MasterManagerBase();

    virtual ~MasterManagerBase() { stop(); };

    void initCfg();
    bool init();
    void updateTopologyCfg(const Sf1rTopology& cfg);

    void start();

    void stop();

    /**
     * Register aggregators
     * @param aggregator
     */
    void registerAggregator(boost::shared_ptr<AggregatorBase> aggregator, bool readonly = false)
    {
        if (!aggregator)
            return;
        boost::lock_guard<boost::mutex> lock(state_mutex_);
        if (readonly)
            readonly_aggregatorList_.push_back(aggregator);
        else
        {
            aggregatorList_.push_back(aggregator);
        }
        resetAggregatorConfig(aggregator, readonly);
    }

    void unregisterAggregator(boost::shared_ptr<AggregatorBase> aggregator, bool readonly = false)
    {
        if (!aggregator)
            return;
        boost::lock_guard<boost::mutex> lock(state_mutex_);

        std::vector<boost::shared_ptr<AggregatorBase> >::iterator it;
        std::vector<boost::shared_ptr<AggregatorBase> >::iterator it_end;
        if (readonly)
        {
            it = readonly_aggregatorList_.begin();
            it_end = readonly_aggregatorList_.end();
        }
        else
        {
            it = aggregatorList_.begin();
            it_end = aggregatorList_.end();
        }
        for (; it != it_end; ++it)
        {
            if ((*it).get() == aggregator.get())
            {
                if (readonly)
                {
                    readonly_aggregatorList_.erase(it);
                }
                else
                {
                    aggregatorList_.erase(it);
                }
                break;
            }
        }
    }

    /**
     * Get data receiver address by shard id.
     * @param shardid   [IN]
     * @param host      [OUT]
     * @param recvPort  [OUT]
     * @return true on success, false on failure.
     */
    bool getShardReceiver(
            shardid_t shardid,
            std::string& host,
            unsigned int& recvPort);

    //bool getCollectionShardids(const std::string& service, const std::string& collection, std::vector<shardid_t>& shardidList);

    //bool checkCollectionShardid(const std::string& service, const std::string& collection, unsigned int shardid);

    void registerIndexStatus(const std::string& collection, bool isIndexing);

    void enableDistribute(bool enable)
    {
        isDistributeEnable_ = enable;
    }

    inline bool isDistributed()
    {
        return isDistributeEnable_;
    }

    void notifyChangedPrimary(bool is_new_primary = true);
    void updateMasterReadyForNew(bool is_ready);

    bool isMinePrimary();
    bool isBusy();
    bool prepareWriteReq();
    bool endWriteReq();
    void endPreparedWrite();
    bool disableNewWrite();
    void enableNewWrite();
    bool pushWriteReq(const std::string& reqdata, const std::string& type = "");
    // make sure prepare success before call this.
    bool popWriteReq(std::string& reqdata, std::string& type);

    //bool getWriteReqDataFromPreparedNode(std::string& req_json_data);
    void setCallback(EventCBType on_new_req_available)
    {
        on_new_req_available_ = on_new_req_available;
    }
    void registerDistributeServiceMaster(boost::shared_ptr<IDistributeService> sp_service, bool enable_master);
    bool findServiceMasterAddress(const std::string& service, std::string& host, uint32_t& port);
    void updateServiceReadState(const std::string& my_state, bool include_self);
    bool hasAnyCachedRequest();
    shardid_t getMyShardId();

    bool isAllShardNodeOK(const std::vector<shardid_t>& shardids);
    bool pushWriteReqToShard(const std::string& reqdata,
        const std::vector<shardid_t>& shardids, bool for_migrate = false,
        bool include_self = false);

    bool notifyAllShardingBeginMigrate(const std::vector<shardid_t>& shardids);
    bool waitForMigrateReady(const std::vector<shardid_t>& shardids);
    bool waitForNewShardingNodes(const std::vector<shardid_t>& shardids);
    void waitForMigrateIndexing(const std::vector<shardid_t>& shardids);
    void notifyAllShardingEndMigrate();
    bool isMineNewSharding();
    std::string getShardNodeIP(shardid_t shardid);
    bool isOnlyMaster();
    bool isMasterEnabled();
    bool isShardingNodeOK(const std::vector<shardid_t>& shardids);

public:
    virtual void process(ZooKeeperEvent& zkEvent);

    virtual void onNodeCreated(const std::string& path);

    virtual void onNodeDeleted(const std::string& path);

    virtual void onChildrenChanged(const std::string& path);
    virtual void onDataChanged(const std::string& path);

    /// test
    void showWorkers();

protected:
    static std::string getReplicaPath(replicaid_t replicaId)
    {
        return ZooKeeperNamespace::getReplicaPath(replicaId);
    }
    static std::string getNodePath(replicaid_t replicaId, nodeid_t nodeId)
    {
        return ZooKeeperNamespace::getNodePath(replicaId, nodeId);
    }
    static std::string getPrimaryNodeParentPath(nodeid_t nodeId)
    {
        return ZooKeeperNamespace::getPrimaryNodeParentPath(nodeId);
    }

protected:
    std::string state2string(MasterStateType e);

    void watchAll();

    bool checkZooKeeperService();

    void doStart();

    bool isPrimaryWorker(replicaid_t replicaId, nodeid_t nodeId);

protected:
    int detectWorkers();
    void detectReadOnlyWorkers(const std::string& nodepath, bool is_created_node);
    void detectReadOnlyWorkersInReplica(replicaid_t replicaId);

    int detectWorkersInReplica(replicaid_t replicaId, size_t& detected, size_t& good);

    void updateWorkerNode(boost::shared_ptr<Sf1rNode>& workerNode, ZNode& znode);

    void detectReplicaSet(const std::string& zpath="");

    /**
     * If any node in current cluster replica is broken down,
     * switch to another node in its replica set.
     */
    void failover(const std::string& zpath);

    bool failover(boost::shared_ptr<Sf1rNode>& sf1rNode);

    /**
     * Recover after nodes in current cluster replica came back from failure.
     */
    void recover(const std::string& zpath);

    /**
     * Register SF1 Server atfer master ready.
     */
    void registerServiceServer();
    void initServices();
    void setServicesData(ZNode& znode);

    /***/
    void resetAggregatorConfig();
    void resetAggregatorConfig(boost::shared_ptr<AggregatorBase>& aggregator, bool readonly);
    void resetReadOnlyAggregatorConfig();

    bool getWriteReqNodeData(ZNode& znode);
    //void putWriteReqDataToPreparedNode(const std::string& req_json_data);
    void checkForWriteReq();
    //void checkForWriteReqFinished();
    void checkForNewWriteReq();
    bool cacheNewWriteFromZNode();
    bool isAllWorkerIdle(bool include_self = true);
    //bool isAllWorkerFinished();
    bool isAllWorkerInState(bool include_self, int state);
    std::string findReCreatedServerPath();
    void updateServiceReadStateWithoutLock(const std::string& my_state, bool include_self);
    bool isWriteQueueEmpty(const std::vector<shardid_t>& shardids);
    bool getNodeState(const std::string& nodepath, uint32_t& state);
    void resetAggregatorBusyState();

protected:
    Sf1rTopology sf1rTopology_;
    bool isDistributeEnable_;

    ZooKeeperClientPtr zookeeper_;

    // znode paths
    std::string topologyPath_;
    std::string serverParentPath_;
    std::string serverPath_;
    std::string serverRealPath_;

    MasterStateType masterState_;
    boost::mutex state_mutex_;

    std::vector<replicaid_t> replicaIdList_;
    //boost::mutex replica_mutex_;

    WorkerMapT workerMap_;
    // handle only read request.
    ROWorkerMapT readonly_workerMap_;
    //boost::mutex workers_mutex_;

    std::vector<boost::shared_ptr<AggregatorBase> > aggregatorList_;
    std::vector<boost::shared_ptr<AggregatorBase> > readonly_aggregatorList_;
    EventCBType on_new_req_available_;
    std::string write_req_queue_root_parent_;
    std::string write_req_queue_parent_;
    std::string write_req_queue_;
    std::string write_prepare_node_;
    std::string write_prepare_node_parent_;
    std::string migrate_prepare_node_;
    bool stopping_;
    bool write_prepared_;
    bool new_write_disabled_;
    bool is_mine_primary_;
    bool is_ready_for_new_write_;
    std::size_t waiting_request_num_;
    std::queue< std::pair<std::string, std::pair<std::string, std::string> > > cached_write_reqlist_;

    std::string CLASSNAME;
    typedef std::map<std::string, boost::shared_ptr<IDistributeService> > ServiceMapT;
    ServiceMapT  all_distributed_services_;
};



}

#endif /* MASTER_MANAGER_BASE_H_ */
