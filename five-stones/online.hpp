#ifndef FIVE_STONES_ONLINE_MANAGER_HPP
#define FIVE_STONES_ONLINE_MANAGER_HPP

#include <cstdint>
#include <mutex>
#include <unordered_map>

// 在线用户管理：维护 uid -> WebSocket连接 的映射（大厅/房间两类）。
class online_manager
{
private:
    std::unordered_map<uint64_t, wsserver_t::connection_ptr> _game_hall;
    std::unordered_map<uint64_t, wsserver_t::connection_ptr> _game_room;
    mutable std::mutex _mutex;

    static bool conn_is_online(const wsserver_t::connection_ptr &conn)
    {
        return (conn != nullptr && conn->get_state() == websocketpp::session::state::open);
    }

public:
    // 进入游戏大厅：连接建立成功后调用
    void enter_game_hall(uint64_t uid, const wsserver_t::connection_ptr &conn)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _game_hall[uid] = conn;
    }

    // 退出游戏大厅：连接断开后调用
    void exit_game_hall(uint64_t uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _game_hall.erase(uid);
    }

    // 进入游戏房间：连接建立成功后调用
    void enter_game_room(uint64_t uid, const wsserver_t::connection_ptr &conn)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _game_room[uid] = conn;
    }

    // 退出游戏房间：连接断开后调用
    void exit_game_room(uint64_t uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _game_room.erase(uid);
    }

    // 判断用户是否在游戏大厅（且连接仍在线）
    bool in_game_hall(uint64_t uid) const
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _game_hall.find(uid);
        return (it != _game_hall.end() && conn_is_online(it->second));
    }

    // 判断用户是否在游戏房间（且连接仍在线）
    bool in_game_room(uint64_t uid) const
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _game_room.find(uid);
        return (it != _game_room.end() && conn_is_online(it->second));
    }

    // 是否在线（大厅或房间任一处且连接仍在线）
    bool is_online(uint64_t uid) const
    {
        return in_game_hall(uid) || in_game_room(uid);
    }

    // 是否掉线（不在线）
    bool is_offline(uint64_t uid) const
    {
        return !is_online(uid);
    }

    // 根据 uid 从大厅获取可通信连接
    wsserver_t::connection_ptr get_conn_from_game_hall(uint64_t uid) const
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _game_hall.find(uid);
        if (it == _game_hall.end() || !conn_is_online(it->second))
            return nullptr;
        return it->second;
    }

    // 根据 uid 从房间获取可通信连接
    wsserver_t::connection_ptr get_conn_from_game_room(uint64_t uid) const
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _game_room.find(uid);
        if (it == _game_room.end() || !conn_is_online(it->second))
            return nullptr;
        return it->second;
    }

    // 同时踢出大厅/房间中的同一用户（比如登出或异常掉线清理）
    void kick(uint64_t uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _game_hall.erase(uid);
        _game_room.erase(uid);
    }
};

#endif

