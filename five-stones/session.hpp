#ifndef FIVE_STONES_SESSION_HPP
#define FIVE_STONES_SESSION_HPP

#include "util.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#define SESSION_TIMEOUT 30000
#define SESSION_FOREVER -1

typedef enum
{
    UNLOGIN,
    LOGIN
} ss_statu;

class session
{
private:
    uint64_t _ssid = 0; // session id
    uint64_t _uid = 0;  // user id
    ss_statu _statu = UNLOGIN;
    wsserver_t::timer_ptr _tp;

public:
    session(uint64_t ssid)
        : _ssid(ssid), _uid(0), _statu(UNLOGIN), _tp()
    {
        DBG_LOG("SESSION %p 被创建！！", static_cast<void *>(this));
    }

    ~session()
    {
        DBG_LOG("SESSION %p 被释放！！", static_cast<void *>(this));
    }

    uint64_t ssid() const { return _ssid; }

    void set_statu(ss_statu statu) { _statu = statu; }
    void set_user(uint64_t uid) { _uid = uid; }
    uint64_t get_user() const { return _uid; }
    bool is_login() const { return (_statu == LOGIN); }

    void set_timer(const wsserver_t::timer_ptr &tp) { _tp = tp; }
    wsserver_t::timer_ptr &get_timer() { return _tp; }
};

using session_ptr = std::shared_ptr<session>;

class session_manager
{
private:
    uint64_t _next_ssid = 1;
    std::mutex _mutex;
    std::unordered_map<uint64_t, session_ptr> _session;
    wsserver_t *_server = nullptr; // 定时器接口依赖的服务器壳子

public:
    explicit session_manager(wsserver_t *srv) : _next_ssid(1), _server(srv)
    {
        DBG_LOG("session管理器初始化完毕！");
    }

    ~session_manager()
    {
        DBG_LOG("session管理器即将销毁！");
    }

    // 创建 session：成功返回 session 指针，失败返回空指针
    session_ptr create_session(uint64_t uid, ss_statu statu)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        session_ptr ssp(new session(_next_ssid));
        ssp->set_statu(statu);
        ssp->set_user(uid);
        _session[_next_ssid] = ssp;
        _next_ssid++;
        return ssp;
    }

    // 添加 session
    void append_session(const session_ptr &ssp)
    {
        if (!ssp)
            return;
        std::unique_lock<std::mutex> lock(_mutex);
        _session[ssp->ssid()] = ssp;
    }

    // 根据 session ID 获取 session 指针：成功返回 session 指针，失败返回空指针
    session_ptr get_session_by_ssid(uint64_t ssid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _session.find(ssid);
        if (it == _session.end())
            return session_ptr();
        return it->second;
    }

    // 删除 session
    void remove_session(uint64_t ssid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _session.erase(ssid);
    }

    // 依赖 wsserver_t 定时器管理 session 超时；进入大厅/房间可用 SESSION_FOREVER 取消超时。
    void set_session_expire_time(uint64_t ssid, int ms)
    {
        session_ptr ssp = get_session_by_ssid(ssid); // 获取 session 指针
        if (ssp.get() == nullptr)
            return;

        wsserver_t::timer_ptr tp = ssp->get_timer(); // 获取 session 定时器句柄

        if (tp.get() == nullptr) // 没有定时器
        {
            if (ms == SESSION_FOREVER)
            {
                // 1. 在session永久存在的情况下，设置永久存在
                return;
            }
            else if (ms != SESSION_FOREVER)
            {
                // 2. 在session不是永久存在的情况下，设置指定时间之后被删除的定时任务
                wsserver_t::timer_ptr tmp_tp = _server->set_timer(
                    ms,
                    [this, ssid](const websocketpp::lib::error_code &ec)
                    {
                        // 仅在定时器正常触发时删除；cancel 导致的 operation_aborted 不删除
                        if (ec)
                            return;
                        this->remove_session(ssid);
                    });
                ssp->set_timer(tmp_tp);
            }
        }
        else if (tp.get() != nullptr)
        {
            if (ms == SESSION_FOREVER)
            {
                // 3. 在session设置了定时删除的情况下，且重置为永久存在：取消旧任务后将session设置为永久存在（session仍在map中，无需append）
                tp->cancel();                            // 取消旧的超时删除任务
                ssp->set_timer(wsserver_t::timer_ptr()); // 将session关联的定时器设置为空
            }
            else if (ms != SESSION_FOREVER)
            {
                // 4. 有定时器且重置超时：取消旧任务后重新计时（session 仍在 map 中，无需 append）
                // 取消旧任务 -> 新建新任务 -> 更新句柄
                tp->cancel();                            // 先取消旧定时任务
                ssp->set_timer(wsserver_t::timer_ptr()); // 清空旧 timer 句柄
                // 重新给session添加定时销毁任务
                wsserver_t::timer_ptr tmp_tp = _server->set_timer(
                    ms,
                    [this, ssp](const websocketpp::lib::error_code &ec)
                    {
                        // 仅在定时器正常触发时删除；cancel 导致的 operation_aborted 不删除
                        if (ec)
                            return;
                        this->remove_session(ssp->ssid());
                    });
                // 重新设置session关联的定时器
                ssp->set_timer(tmp_tp);
            }
        }
    }
};

#endif
