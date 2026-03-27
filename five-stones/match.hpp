#ifndef FIVE_STONES_MATCHER_HPP
#define FIVE_STONES_MATCHER_HPP

#include "util.hpp"
#include "online.hpp"
#include "db.hpp"
#include "room.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <mutex>
#include <thread>

template <class T>
class match_queue
{
private:
    // 用链表而不直接使用 queue，是因为有中间删除数据的需要
    std::list<T> _list;
    std::mutex _mutex;             // 实现线程安全
    std::condition_variable _cond; // 主要为了阻塞消费者，后边使用的时候：队列中元素个数<2则阻塞

public:
    // 获取队列元素个数
    int size()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return static_cast<int>(_list.size());
    }

    // 判断队列是否为空
    bool empty()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _list.empty();
    }

    // 阻塞等待（队列人数不足时供匹配线程等待）
    void wait()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        // _cond.wait(lock);
        // 使用谓词避免“先 notify 后 wait”导致的丢唤醒
        _cond.wait(lock, [this]()
                   { return _list.size() >= 2; });
        //    如果 wait() 是不带谓词版本，就会出现这个竞态窗口：
        //    1.匹配线程执行 mq.size()，看到只有 1 人，准备去 wait
        //    2.在它真正 wait 前，第二个同分玩家 push 进队列并 notify_all
        //    3.因为此时匹配线程还没睡，这次通知“打空”
        //    4.接着匹配线程调用无谓词 wait，直接睡下去
        //    5.队列其实已是 2 人，但没有新通知了，于是这俩人卡住，匹配不成功（常要等第三人触发下一次 notify 才恢复）
        //    这就是你看到“两个同分的人不能成功匹配”的最常见原因
    }

    // 入队数据，并唤醒线程
    void push(const T &data)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _list.push_back(data);
        _cond.notify_all(); // 唤醒所有等待的线程
    }

    // 出队数据
    bool pop(T &data)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_list.empty())
            return false;     // 队列为空，返回false
        data = _list.front(); // 队列不为空，取出队列头元素
        _list.pop_front();
        return true; // 队列不为空，取出队列头元素成功，返回true
    }

    // 移除指定的数据
    void remove(const T &data)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _list.remove(data); // 移除指定的数据
    }
};

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
