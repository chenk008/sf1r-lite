#ifndef LOG_SERVER_PROCESS_H_
#define LOG_SERVER_PROCESS_H_

#include "LogServerCfg.h"
#include "RpcLogServer.h"
#include "DocidDispatcher.h"

#include <am/leveldb/Table.h>
#include <3rdparty/am/drum/drum.hpp>
#include <glog/logging.h>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

namespace sf1r
{

class LogServerProcess
{
    typedef izenelib::drum::Drum<
        uint64_t,
        std::string,
        std::string,
        64,
        4096,
        65536,
        izenelib::am::leveldb::TwoPartComparator,
        izenelib::am::leveldb::Table,
        DocidDispatcher
    > DrumType;

public:
    LogServerProcess();

    bool init(const std::string& cfgFile);

    /// @brief start all
    void start();

    /// @brief join all
    void join();

    /// @brief interrupt
    void stop();

private:
    bool initRpcLogServer();

private:
    LogServerCfg logServerCfg_;

    boost::scoped_ptr<RpcLogServer> rpcLogServer_;

    boost::scoped_ptr<DrumType> drum_;
};

}

#endif /* LOG_SERVER_PROCESS_H_ */