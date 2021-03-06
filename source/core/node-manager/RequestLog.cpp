#include "RequestLog.h"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace sf1r
{
std::set<std::string> ReqLogMgr::write_req_set_;
std::set<std::string> ReqLogMgr::replay_write_req_set_;
std::set<std::string> ReqLogMgr::auto_shard_write_set_;

// to handle write request correctly , you need do things below:
// 1. add controller_action string to ReqLogMgr, and define the log type for it if neccesary.
// If this kind of request need backup data before processing, you should add it to DistributeRequestHooker.
// 2. In Controller Handler, the preprocess in base Sf1Controller will handle the distribute properly,
//  and hook the request if needed. But if the request need to be sharded to other
//  node, make sure call the rpc method HookDistributeRequest again first to make sure all 
//  sharded node hooked this request. The Controller which is not derived from Sf1Controller
//  need to handle this by themselves.
// 3. In Service (such as IndexTaskService or RecommendTaskService), make sure 
// the request was hooked to shard node before call the worker handler.
// 4. In worker handler, make sure using DistributeRequestHooker to check valid call and
// prepare before do actual work, call processLocalFinished after finish to make sure the 
// request is hooked to handle correctly both on the primary and replicas.
// If returned before prepared, make sure processLocalFinished also called.
// 5. make sure if the hooktype == FromLog, all the handler should be called sync. (not in JobScheduler)
// 6. if a write request need to chain with( a write after write or 
// a write during the write ) another write request, you should 
// set the chain status properly before you call the request.
void ReqLogMgr::initWriteRequestSet()
{
    write_req_set_.insert("documents_create");
    write_req_set_.insert("documents_destroy");
    write_req_set_.insert("documents_update");
    write_req_set_.insert("documents_update_inplace");
    write_req_set_.insert("documents_set_top_group_label");
    write_req_set_.insert("documents_log_group_label");
    write_req_set_.insert("documents_visit");
    write_req_set_.insert("collection_start_collection");
    write_req_set_.insert("collection_stop_collection");
    write_req_set_.insert("collection_update_collection_conf");
    write_req_set_.insert("collection_rebuild_from_scd");
    write_req_set_.insert("collection_backup_all");
    write_req_set_.insert("collection_set_kv");

    write_req_set_.insert("collection_update_sharding_conf");

    write_req_set_.insert("commands_index");
    write_req_set_.insert("commands_index_recommend");
    write_req_set_.insert("commands_mining");
    write_req_set_.insert("commands_optimize_index");
    write_req_set_.insert("commands_index_query_log");
    write_req_set_.insert("faceted_set_custom_rank");
    write_req_set_.insert("faceted_set_merchant_score");
    write_req_set_.insert("faceted_set_ontology");
    write_req_set_.insert("keywords_inject_query_correction");
    write_req_set_.insert("keywords_inject_query_recommend");

    write_req_set_.insert("recommend_add_user");
    write_req_set_.insert("recommend_update_user");
    write_req_set_.insert("recommend_remove_user");
    write_req_set_.insert("recommend_purchase_item");
    write_req_set_.insert("recommend_rate_item");
    write_req_set_.insert("recommend_visit_item");
    write_req_set_.insert("recommend_update_shopping_cart");
    write_req_set_.insert("recommend_track_event");

    //replay_write_req_set_.insert("documents_update");
    //replay_write_req_set_.insert("documents_update_inplace");
    replay_write_req_set_.insert("documents_set_top_group_label");
    replay_write_req_set_.insert("documents_log_group_label");
    replay_write_req_set_.insert("documents_visit");
    replay_write_req_set_.insert("faceted_set_custom_rank");
    replay_write_req_set_.insert("faceted_set_merchant_score");
    replay_write_req_set_.insert("faceted_set_ontology");
    replay_write_req_set_.insert("recommend_add_user");
    replay_write_req_set_.insert("recommend_update_user");
    replay_write_req_set_.insert("recommend_remove_user");
    replay_write_req_set_.insert("recommend_purchase_item");
    replay_write_req_set_.insert("recommend_rate_item");
    replay_write_req_set_.insert("recommend_visit_item");
    replay_write_req_set_.insert("recommend_update_shopping_cart");
    replay_write_req_set_.insert("recommend_track_event");

    auto_shard_write_set_.insert("documents_set_top_group_label");
    auto_shard_write_set_.insert("documents_log_group_label");
    auto_shard_write_set_.insert("commands_mining");
    auto_shard_write_set_.insert("commands_optimize_index");
    auto_shard_write_set_.insert("commands_index_query_log");
    auto_shard_write_set_.insert("faceted_set_custom_rank");
    auto_shard_write_set_.insert("faceted_set_merchant_score");
    auto_shard_write_set_.insert("faceted_set_ontology");
    auto_shard_write_set_.insert("keywords_inject_query_correction");
    auto_shard_write_set_.insert("keywords_inject_query_recommend");

    auto_shard_write_set_.insert("recommend_add_user");
    auto_shard_write_set_.insert("recommend_update_user");
    auto_shard_write_set_.insert("recommend_remove_user");
    auto_shard_write_set_.insert("recommend_purchase_item");
    auto_shard_write_set_.insert("recommend_rate_item");
    auto_shard_write_set_.insert("recommend_visit_item");
    auto_shard_write_set_.insert("recommend_update_shopping_cart");
    auto_shard_write_set_.insert("recommend_track_event");

}

void ReqLogMgr::init(const std::string& basepath)
{
    boost::lock_guard<boost::mutex> lock(lock_);
    inc_id_ = 1;
    last_writed_id_ = 0;
    base_path_ = basepath;
    head_log_path_ = basepath + "/head.req.log";
    std::vector<CommonReqData>().swap(prepared_req_);
    loadLastData();
}

bool ReqLogMgr::prepareReqLog(CommonReqData& prepared_reqdata, bool isprimary)
{
    boost::lock_guard<boost::mutex> lock(lock_);
    if (!prepared_req_.empty())
    {
        std::cerr << "already has a prepared Request, only one write request allowed." << endl;
        return false;
    }
    if (isprimary)
        prepared_reqdata.inc_id = inc_id_++;
    else
    {
        if (prepared_reqdata.inc_id < inc_id_)
        {
            std::cerr << "!!!! prepared failed !!!. A prepared request from primary has lower inc_id than replica." << endl;
            return false;
        }
        inc_id_ = prepared_reqdata.inc_id + 1;
    }
    //prepared_reqdata.json_data_size = prepared_reqdata.req_json_data.size();
    prepared_req_.push_back(prepared_reqdata);
    //printf("prepare request success, isprimary %d, (id:%u, type:%u) ", isprimary,
    //    prepared_reqdata.inc_id, prepared_reqdata.reqtype);
    //std::cout << endl;
    return true;
}

bool ReqLogMgr::getPreparedReqLog(CommonReqData& reqdata)
{
    boost::lock_guard<boost::mutex> lock(lock_);
    if (prepared_req_.empty())
        return false;
    reqdata = prepared_req_.back();
    return true;
}

void ReqLogMgr::delPreparedReqLog()
{
    boost::lock_guard<boost::mutex> lock(lock_);
    std::vector<CommonReqData>().swap(prepared_req_);
}

bool ReqLogMgr::appendReqData(const std::string& req_packed_data)
{
    boost::lock_guard<boost::mutex> lock(lock_);
    if (prepared_req_.empty())
        return false;
    const CommonReqData& reqdata = prepared_req_.back();
    if (reqdata.inc_id < last_writed_id_)
    {
        std::cerr << "append error!!! Request log must append in order by inc_id. " << std::endl;
        return false;
    }
    ReqLogHead whead;
    whead.inc_id = reqdata.inc_id;
    whead.reqtype = reqdata.reqtype;
    std::ofstream ofs(getDataPath(whead.inc_id).c_str(), ios::app|ios::binary|ios::ate|ios::out);
    std::ofstream ofs_head(head_log_path_.c_str(), ios::app|ios::binary|ios::ate|ios::out);
    if (!ofs.good() || !ofs_head.good())
    {
        std::cerr << "append error!!! Request log open failed. " << std::endl;
        return false;
    }
    whead.req_data_offset = ofs.tellp();
    whead.req_data_len = req_packed_data.size();
    whead.req_data_crc = crc(0, req_packed_data.data(), req_packed_data.size());

    //using namespace boost::posix_time;
    //static ptime epoch(boost::gregorian::date(1970, 1, 1));
    //ptime current_time = microsec_clock::universal_time();
    //time_duration td = current_time - epoch;
    //std::string time_str = boost::lexical_cast<std::string>(double(td.total_milliseconds())/1000);
    //time_str.resize(15, '\0');
    //time_str.append(1, '\0');
    //memcpy(&whead.reqtime[0], time_str.data(), sizeof(whead.reqtime));
    ofs.write(req_packed_data.data(), req_packed_data.size());
    ofs_head.write((const char*)&whead, sizeof(whead));
    last_writed_id_ = whead.inc_id;
    ofs.close();
    ofs_head.close();
    return true;
}

// you can use this to read all request data in sequence.
bool ReqLogMgr::getReqDataByHeadOffset(size_t& headoffset, ReqLogHead& rethead, std::string& req_packed_data)
{
    boost::lock_guard<boost::mutex> lock(lock_);
    std::ifstream ifs(head_log_path_.c_str(), ios::binary|ios::in);
    if (!ifs.good())
    {
        std::cerr << "error!!! Request log open failed. " << std::endl;
        throw std::runtime_error("open request log failed.");
    }
    ifs.seekg(0, ios::end);
    size_t length = ifs.tellg();
    if (length < sizeof(ReqLogHead))
        return false;
    if (headoffset > length - sizeof(ReqLogHead))
        return false;
    rethead = getHeadData(ifs, headoffset);
    // update offset to next head position.
    headoffset += sizeof(ReqLogHead);
    return getReqPackedDataByHead(rethead, req_packed_data);
}

// get request with inc_id or the minimize that not less than inc_id if not exist.
// if no more request return false.
bool ReqLogMgr::getReqData(uint32_t& inc_id, ReqLogHead& rethead, size_t& headoffset, std::string& req_packed_data)
{
    boost::lock_guard<boost::mutex> lock(lock_);
    if (!getHeadOffsetWithoutLock(inc_id, rethead, headoffset))
        return false;
    return getReqPackedDataByHead(rethead, req_packed_data);
}

bool ReqLogMgr::getHeadOffset(uint32_t& inc_id, ReqLogHead& rethead, size_t& headoffset)
{
    boost::lock_guard<boost::mutex> lock(lock_);
    return getHeadOffsetWithoutLock(inc_id, rethead, headoffset);
}

bool ReqLogMgr::getHeadOffsetWithoutLock(uint32_t& inc_id, ReqLogHead& rethead, size_t& headoffset)
{
    std::ifstream ifs(head_log_path_.c_str(), ios::binary|ios::in);
    if (!ifs.good())
    {
        std::cerr << "error!!! Request log open failed. " << std::endl;
        throw std::runtime_error("open request log file failed.");
    }
    ifs.seekg(0, ios::end);
    size_t length = ifs.tellg();
    if (length < sizeof(ReqLogHead))
        return false;
    assert(length%sizeof(ReqLogHead) == 0);
    size_t start = 0;
    size_t end = length/sizeof(ReqLogHead) - 1;
    ReqLogHead cur = getHeadData(ifs, end*sizeof(ReqLogHead));
    if (inc_id > cur.inc_id)
        return false;
    uint32_t ret_id = inc_id;
    while(end >= start)
    {
        size_t mid = (end - start)/2 + start;
        cur = getHeadData(ifs, mid*sizeof(ReqLogHead));
        if (cur.inc_id > inc_id)
        {
            ret_id = cur.inc_id;
            rethead = cur;
            headoffset = mid*sizeof(ReqLogHead);
            if (mid == 0)
                break;
            end = mid - 1;
        }
        else if (cur.inc_id < inc_id)
        {
            start = mid + 1;
        }
        else
        {
            ret_id = inc_id;
            rethead = cur;
            headoffset = mid*sizeof(ReqLogHead);
            break;
        }
    }
    //std::cout << "get head offset by inc_id: " << inc_id << ", returned nearest id:" << ret_id << std::endl;
    inc_id = ret_id;
    return true;
}

bool ReqLogMgr::getReqPackedDataByHead(const ReqLogHead& head, std::string& req_packed_data)
{
    std::ifstream ifs_data(getDataPath(head.inc_id).c_str(), ios::binary|ios::in);
    if (!ifs_data.good())
    {
        std::cerr << "error!!! Request log open failed. " << std::endl;
        throw std::runtime_error("open request log file failed.");
    }
    req_packed_data.resize(head.req_data_len, '\0');
    ifs_data.seekg(head.req_data_offset, ios::beg);
    ifs_data.read((char*)&req_packed_data[0], head.req_data_len);
    if (crc(0, req_packed_data.data(), req_packed_data.size()) != head.req_data_crc)
    {
        std::cerr << "warning: crc check failed for request log data." << std::endl;
        throw std::runtime_error("request log data corrupt.");
    }
    return true;
}

std::string ReqLogMgr::getDataPath(uint32_t inc_id)
{
    std::stringstream ss;
    ss << base_path_ << "/" << inc_id/100000 << ".req.log";
    return ss.str();
}

ReqLogHead ReqLogMgr::getHeadData(std::ifstream& ifs, size_t offset)
{
    ReqLogHead head;
    assert(offset%sizeof(ReqLogHead) == 0);
    ifs.seekg(offset, ios::beg);
    ifs.read((char*)&head, sizeof(head));
    return head;
}

void ReqLogMgr::loadLastData()
{
    if (boost::filesystem::exists(base_path_))
    {
        std::ifstream ifs(head_log_path_.c_str(), ios::binary|ios::in);
        if (ifs.good())
        {
            ifs.seekg(0, ios::end);
            int length = ifs.tellg();
            if (length == 0)
            {
                std::cerr << "no request since last down." << std::endl;
                return;
            }
            else if (length < (int)sizeof(ReqLogHead))
            {
                std::cerr << "read request log head file error. length :" << length << std::endl;
                throw std::runtime_error("read request log head file error");
            }
            else if ( length % sizeof(ReqLogHead) != 0)
            {
                std::cerr << "The head file is corrupt. need restore from last backup. len:" << length << std::endl;
                throw std::runtime_error("read request log head file error");
            }
            ifs.seekg(0 - sizeof(ReqLogHead), ios::end);
            ReqLogHead lasthead;
            ifs.read((char*)&lasthead, sizeof(ReqLogHead));
            inc_id_ = lasthead.inc_id;
            //std::cout << "loading request log for last request: inc_id : " <<
            //    inc_id_ << ", type:" << lasthead.reqtype << std::endl;
            last_writed_id_ = inc_id_;
            ++inc_id_;
        }
    }
    else
    {
        boost::filesystem::create_directories(base_path_);
        std::ofstream ofs_head(head_log_path_.c_str(), ios::app|ios::binary|ios::ate|ios::out);
        if (!ofs_head.good())
            throw std::runtime_error("init log head file failed.");
        ofs_head << std::flush;
        ofs_head.close();
    }
}

void ReqLogMgr::getReqLogIdList(uint32_t start, uint32_t max_return, bool needdata,
    std::vector<uint32_t>& req_logid_list,
    std::vector<std::string>& req_logdata_list)
{
    ReqLogHead head;
    size_t headoffset;
    std::string req_packed_data;
    bool ret = getHeadOffset(start, head, headoffset);
    if (ret)
    {
        max_return = max_return > getLastSuccessReqId() ? getLastSuccessReqId():max_return;
        req_logid_list.reserve(max_return);
        if (needdata)
            req_logdata_list.reserve(max_return);
        while(true)
        {
            ret = getReqDataByHeadOffset(headoffset, head, req_packed_data);

            if(!ret || req_logid_list.size() >= max_return)
                break;
            req_logid_list.push_back(head.inc_id);
            if (needdata)
            {
                req_logdata_list.push_back(req_packed_data);
            }
        }
    }
}

}

