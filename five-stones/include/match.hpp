#ifndef FIVE_STONES_MATCHER_HPP
#define FIVE_STONES_MATCHER_HPP

#include "match_queue.hpp"
#include "util.hpp"
#include "online.hpp"
#include "db.hpp"
#include "room.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

class matcher
{
private:
    match_queue<uint64_t> _q_normal; // 普通选手匹配队列
    match_queue<uint64_t> _q_high;   // 高手匹配队列
    match_queue<uint64_t> _q_super;  // 大神匹配队列

    room_manager *_rm = nullptr;   // 房间管理器
    user_table *_ut = nullptr;     // 用户表
    online_manager *_om = nullptr; // 在线管理器

    std::thread _th_normal; // 普通选手匹配线程
    std::thread _th_high;   // 高手匹配线程
    std::thread _th_super;  // 大神匹配线程

private:
    void handle_match(match_queue<uint64_t> &mq) // 处理匹配
    {
        while (true) // 匹配模块运行时，循环处理匹配
        {
            // 1. 人数不足则阻塞
            while (mq.size() < 2) // 队列中元素个数<2则阻塞
                mq.wait();

            // 2. 出队两个玩家
            uint64_t uid1 = 0, uid2 = 0;
            bool ret = mq.pop(uid1); // 出队第一个玩家
            if (!ret)
                continue;

            ret = mq.pop(uid2); // 出队第二个玩家
            if (!ret)
            {
                add(uid1); // 第一个玩家不在队列中，重新入队
                continue;
            }

            // 3. 校验两人是否还在大厅在线；不在线则把另一人重新入队
            wsserver_t::connection_ptr conn1 = _om->get_conn_from_game_hall(uid1); // 获取第一个玩家的大厅连接
            if (conn1.get() == nullptr)
            {
                add(uid2); // 第二个玩家不在大厅中，重新入队
                continue;
            }

            wsserver_t::connection_ptr conn2 = _om->get_conn_from_game_hall(uid2); // 获取第二个玩家的大厅连接
            if (conn2.get() == nullptr)
            {
                add(uid1); // 第一个玩家不在大厅中，重新入队
                continue;
            }

            // 4. 创建房间
            room_ptr rp = _rm->create_room(uid1, uid2);
            if (rp.get() == nullptr)
            {
                add(uid1);
                add(uid2);
                continue;
            }

            // 5. 匹配成功响应（当前项目中的 connection_ptr 提供 send）
            Json::Value resp;
            resp["optype"] = "match_success";
            resp["result"] = true;
            resp["room_id"] = (Json::UInt64)rp->id();

            std::string body;
            json_util::serialize(resp, body);
            conn1->send(body);
            conn2->send(body);
        }
    }

    void th_normal_entry() { handle_match(_q_normal); } // 普通选手匹配线程
    void th_high_entry() { handle_match(_q_high); }     // 高手匹配线程
    void th_super_entry() { handle_match(_q_super); }   // 大神匹配线程

public:
    matcher(room_manager *rm, user_table *ut, online_manager *om) // 初始化匹配模块
        : _rm(rm), _ut(ut), _om(om),
          _th_normal(std::thread(&matcher::th_normal_entry, this)),
          _th_high(std::thread(&matcher::th_high_entry, this)),
          _th_super(std::thread(&matcher::th_super_entry, this))
    {
        DBG_LOG("游戏匹配模块初始化完毕");
    }

    bool add(uint64_t uid) // 添加玩家到匹配队列
    {
        // 根据玩家分数划分档次，进入不同匹配池
        Json::Value user;
        bool ret = _ut->select_by_id(uid, user);
        if (!ret)
        {
            DBG_LOG("获取玩家:%lu 信息失败！！", uid);
            return false;
        }

        int score = user["score"].asInt();
        if (score < 2000)
            _q_normal.push(uid);
        else if (score < 3000)
            _q_high.push(uid);
        else
            _q_super.push(uid);

        return true;
    }

    bool del(uint64_t uid)
    {
        Json::Value user;
        bool ret = _ut->select_by_id(uid, user);
        if (!ret)
        {
            DBG_LOG("获取玩家:%lu 信息失败！！", uid);
            return false;
        }

        int score = user["score"].asInt();
        if (score < 2000)
            _q_normal.remove(uid);
        else if (score < 3000)
            _q_high.remove(uid);
        else
            _q_super.remove(uid);

        return true;
    }
};

#endif
