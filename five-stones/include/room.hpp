#ifndef FIVE_STONES_ROOM_HPP
#define FIVE_STONES_ROOM_HPP

#include "db.hpp"
#include "online.hpp"
#include "util.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#define BOARD_ROW 15
#define BOARD_COL 15
#define CHESS_WHITE 1
#define CHESS_BLACK 2

typedef enum
{
    GAME_START,
    GAME_OVER
} room_statu;

class room
{
private:
    static constexpr uint64_t TOTAL_INIT_MS = 600000;     // 局时初始：600s
    static constexpr uint64_t STEP_INIT_MS = 30000;       // 步时初始：30s
    static constexpr uint64_t TIMEOUT_TOLERANCE_MS = 200; // constexpr 是 C++11 引入的关键字，指定该变量（或函数）的值在编译期即可确定。

    uint64_t _room_id = 0;
    room_statu _statu = GAME_START;
    int _player_count = 0;
    uint64_t _white_id = 0;                 // 房间中白棋玩家的 uid
    uint64_t _black_id = 0;                 // 房间中黑棋玩家的 uid
    user_table *_tb_user = nullptr;         // 数据库访问接口
    online_manager *_online_user = nullptr; // 在线用户管理接口
    std::vector<std::vector<int>> _board;   // 棋盘状态：0-无子，1-白子，2-黑子

    // 观战连接：uid -> 连接（不参与落子、不影响胜负）
    std::unordered_map<uint64_t, wsserver_t::connection_ptr> _spectator_conns;

    // 计时状态（服务端权威）
    bool _timing_started = false; // 第一次合法落子后开始计时
    uint64_t _turn_uid = 0;       // 当前轮到出手的玩家 uid
    std::chrono::steady_clock::time_point _turn_start_tp;
    uint64_t _white_total_left_ms = 0;
    uint64_t _black_total_left_ms = 0;

    // 再来一局：邀请方 uid（双方都为 player 且游戏结束后才允许发起/同意）
    // 表示当前房间是否存在待处理的 rematch_invite。
    uint64_t _rematch_initiator_uid = 0;

private:
    // 判断是否形成至少5连
    bool five(int row, int col, int row_off, int col_off, int color)
    {
        // 检索以(row,col)为起点，在一个方向上是否形成至少5连
        int count = 1;

        int search_row = row + row_off;
        int search_col = col + col_off;
        while (search_row >= 0 && search_row < BOARD_ROW && search_col >= 0 && search_col < BOARD_COL &&
               _board[search_row][search_col] == color)
        {
            count++;
            search_row += row_off;
            search_col += col_off;
        }

        search_row = row - row_off;
        search_col = col - col_off;
        while (search_row >= 0 && search_row < BOARD_ROW && search_col >= 0 && search_col < BOARD_COL &&
               _board[search_row][search_col] == color)
        {
            count++;
            search_row -= row_off;
            search_col -= col_off;
        }

        return (count >= 5);
    }

    // 判断是否形成至少5连，并返回赢家的 uid（如果没有赢家则返回0）
    uint64_t check_win(int row, int col, int color)
    {
        if (five(row, col, 0, 1, color) || five(row, col, 1, 0, color) || five(row, col, -1, 1, color) ||
            five(row, col, -1, -1, color))
        {
            return color == CHESS_WHITE ? _white_id : _black_id; // 返回赢家的 uid
        }
        return 0;
    }

    // 广播房间内消息：发送给双方玩家
    void broadcast(Json::Value &resp)
    {
        // 1. 对要响应的信息进⾏序列化，将Json::Value中的数据序列化成为json格式字符串
        std::string body;
        json_util::serialize(resp, body);
        // 2. 获取房间中所有用户的通信连接
        // 3. 发送响应信息
        wsserver_t::connection_ptr wconn = _online_user->get_conn_from_game_room(_white_id); // 获取白棋玩家的连接
        if (wconn.get() != nullptr)
        {
            wconn->send(body);
        }
        wsserver_t::connection_ptr bconn = _online_user->get_conn_from_game_room(_black_id); // 获取黑棋玩家的连接
        if (bconn.get() != nullptr)
        {
            bconn->send(body);
        }

        // 观战者也同步收到同样的房间消息（例如 put_chess 的落子更新）
        for (auto &kv : _spectator_conns)
        {
            const wsserver_t::connection_ptr &sconn = kv.second;
            if (sconn.get() != nullptr)
            {
                sconn->send(body);
            }
        }
    }

public:
    room(uint64_t room_id, user_table *tb_user, online_manager *online_user)
        : _room_id(room_id),
          _statu(GAME_START),
          _player_count(0),
          _white_id(0),
          _black_id(0),
          _tb_user(tb_user),
          _online_user(online_user),
          _board(BOARD_ROW, std::vector<int>(BOARD_COL, 0)),
          _turn_start_tp(std::chrono::steady_clock::now())
    {
        DBG_LOG("%lu 房间创建成功!!", _room_id);
    }

    ~room()
    {
        DBG_LOG("%lu 房间销毁成功!!", _room_id);
    }

    uint64_t id() const { return _room_id; }
    room_statu statu() const { return _statu; }
    int player_count() const { return _player_count; }

    void add_white_user(uint64_t uid)
    {
        _white_id = uid;
        _player_count++;
    }
    void add_black_user(uint64_t uid)
    {
        _black_id = uid;
        // 五子棋先手：黑棋先思考出第一步（但计时从第一步落子后开始）
        _turn_uid = uid;
        _player_count++;
    }

    uint64_t get_white_user() const { return _white_id; }
    uint64_t get_black_user() const { return _black_id; }

    void add_spectator(uint64_t uid, const wsserver_t::connection_ptr &conn)
    {
        _spectator_conns[uid] = conn;
    }

    void remove_spectator(uint64_t uid)
    {
        _spectator_conns.erase(uid);
    }

    // 棋盘快照：给新观战者一次性还原当前局面
    Json::Value board_snapshot() const
    {
        Json::Value arr(Json::arrayValue);
        for (int r = 0; r < BOARD_ROW; ++r)
        {
            for (int c = 0; c < BOARD_COL; ++c)
            {
                const int v = _board[r][c]; // 0-空，1-白，2-黑
                if (v != 0)
                {
                    Json::Value cell;
                    cell["row"] = r;
                    cell["col"] = c;
                    cell["color"] = v;
                    arr.append(cell);
                }
            }
        }
        return arr;
    }

    // 处理下棋动作：输入 req，返回 response
    Json::Value handle_chess(Json::Value &req)
    {
        Json::Value json_resp = req;

        const int chess_row = req["row"].asInt();
        const int chess_col = req["col"].asInt();
        const uint64_t cur_uid = req["uid"].asUInt64();
        // 1. 棋盘坐标是否合法
        if (chess_row < 0 || chess_row >= BOARD_ROW || chess_col < 0 || chess_col >= BOARD_COL)
        {
            json_resp["result"] = false;
            json_resp["reason"] = "棋盘坐标非法！";
            return json_resp;
        }

        // 2. 判断双方是否都在线（任意一方掉线则另一方胜利）
        if (!_online_user->in_game_room(_white_id))
        {
            json_resp["result"] = true;
            json_resp["reason"] = "对方掉线，不战而胜！";
            json_resp["winner"] = (Json::UInt64)_black_id;
            return json_resp;
        }
        if (!_online_user->in_game_room(_black_id))
        {
            json_resp["result"] = true;
            json_resp["reason"] = "对方掉线，不战而胜！";
            json_resp["winner"] = (Json::UInt64)_white_id;
            return json_resp;
        }

        // 3. 位置是否已被占用
        if (_board[chess_row][chess_col] != 0)
        {
            json_resp["result"] = false;
            json_resp["reason"] = "当前位置已经有了其他棋子！";
            return json_resp;
        }

        // 4. 落子与胜负判断
        const int cur_color = (cur_uid == _white_id ? CHESS_WHITE : CHESS_BLACK);
        _board[chess_row][chess_col] = cur_color; // 落子

        uint64_t winner_id = check_win(chess_row, chess_col, cur_color);
        if (winner_id != 0)
            json_resp["reason"] = "五星连珠，战无不胜！";

        json_resp["result"] = true;
        json_resp["winner"] = (Json::UInt64)winner_id;
        return json_resp;
    }

    // 处理聊天动作
    Json::Value handle_chat(Json::Value &req)
    {
        Json::Value json_resp = req;

        std::string msg = req["message"].asString();
        size_t pos = msg.find("垃圾"); // 简单的敏感词过滤示例：如果消息中包含“垃圾”则认为不合法
        if (pos != std::string::npos)
        {
            json_resp["result"] = false;
            json_resp["reason"] = "消息中包含敏感词，不能发送！";
            return json_resp;
        }

        json_resp["result"] = true;
        return json_resp;
    }

    // 玩家退出房间动作：掉线或主动退出时调用
    void handle_exit(uint64_t uid)
    {
        Json::Value json_resp;
        if (_statu == GAME_START)
        {
            const uint64_t winner_id = (uid == _white_id) ? _black_id : _white_id;
            const uint64_t loser_id = (uid == _white_id) ? _white_id : _black_id;
            json_resp["optype"] = "put_chess";
            json_resp["result"] = true;
            json_resp["reason"] = "对方掉线，不战而胜！";
            json_resp["room_id"] = (Json::UInt64)_room_id;
            json_resp["uid"] = (Json::UInt64)uid;
            json_resp["row"] = -1;
            json_resp["col"] = -1;
            json_resp["winner"] = (Json::UInt64)winner_id;
            _tb_user->win(winner_id);
            _tb_user->lose(loser_id);
            _statu = GAME_OVER;
            broadcast(json_resp);
        }
        else if (_statu == GAME_OVER)
        {
            json_resp["optype"] = "peer_left_room";
            json_resp["result"] = true;
            json_resp["reason"] = "对方已返回大厅";
            json_resp["room_id"] = (Json::UInt64)_room_id;
            json_resp["uid"] = (Json::UInt64)uid;
            broadcast(json_resp);
        }

        // 房间中玩家数量--
        if (_player_count > 0)
            _player_count--;
    }

    void fill_white_black_usernames(Json::Value &json_resp)
    {
        Json::Value u;
        if (_tb_user->select_by_id(_white_id, u) && !u["username"].isNull())
            json_resp["white_username"] = u["username"];
        else
            json_resp["white_username"] = "";

        if (_tb_user->select_by_id(_black_id, u) && !u["username"].isNull())
            json_resp["black_username"] = u["username"];
        else
            json_resp["black_username"] = "";
    }

    bool both_players_online() const
    {
        return _online_user->in_game_room(_white_id) && _online_user->in_game_room(_black_id);
    }

    // 触发再来一局：交换白/黑、重置棋盘/计时，并返回要广播的 rematch_start 响应
    Json::Value start_rematch()
    {
        // 1) 交换角色：下一局让黑先手（前端 myTurn 逻辑与现有约定保持一致）
        std::swap(_white_id, _black_id);

        // 2) 重置棋盘
        for (int r = 0; r < BOARD_ROW; ++r)
        {
            for (int c = 0; c < BOARD_COL; ++c)
            {
                _board[r][c] = 0;
            }
        }

        // 3) 重置房间状态
        _statu = GAME_START;
        _timing_started = false;
        _turn_uid = _black_id;
        _turn_start_tp = std::chrono::steady_clock::now();

        _white_total_left_ms = TOTAL_INIT_MS;
        _black_total_left_ms = TOTAL_INIT_MS;
        _rematch_initiator_uid = 0;

        // 4) 准备广播消息
        Json::Value json_resp;
        json_resp["optype"] = "rematch_start";
        json_resp["result"] = true;
        json_resp["room_id"] = (Json::UInt64)_room_id;
        json_resp["white_id"] = (Json::UInt64)_white_id;
        json_resp["black_id"] = (Json::UInt64)_black_id;

        fill_white_black_usernames(json_resp);

        json_resp["timing_started"] = false;
        json_resp["total_init_ms"] = (Json::UInt64)TOTAL_INIT_MS;
        json_resp["step_init_ms"] = (Json::UInt64)STEP_INIT_MS;
        json_resp["board_snapshot"] = Json::Value(Json::arrayValue); // 清空，前端重绘即可
        return json_resp;
    }

    // 总的请求处理函数：区分 put_chess/chat
    void handle_request(Json::Value &req)
    {
        Json::Value json_resp; // 响应的 Json 对象
        const uint64_t room_id = req["room_id"].asUInt64();
        if (room_id != _room_id)
        {
            json_resp["optype"] = req["optype"].asString();
            json_resp["result"] = false;
            json_resp["reason"] = "房间号不匹配！";
            broadcast(json_resp);
            return;
        }

        const std::string optype = req["optype"].asString();
        if (optype == "put_chess")
        {
            const uint64_t uid = req["uid"].asUInt64();
            if (_statu == GAME_OVER)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "游戏已经结束了！";
            }
            else
            {
                // 轮次校验：计时开始后仅允许当前轮到的玩家落子
                if (_timing_started && uid != _turn_uid)
                {
                    json_resp["optype"] = optype;
                    json_resp["result"] = false;
                    json_resp["reason"] = "还没轮到你";
                    broadcast(json_resp);
                    return;
                }

                uint64_t elapsed_ms = 0;
                if (_timing_started) // 如果计时已经开始
                {
                    const auto now_tp = std::chrono::steady_clock::now();                                                          // 获取当前时间
                    elapsed_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - _turn_start_tp).count(); // 计算步时

                    const uint64_t cur_uid = _turn_uid;
                    const uint64_t cur_total_left_ms = (cur_uid == _white_id) ? _white_total_left_ms : _black_total_left_ms; // 计算剩余局时
                    const bool step_expired = elapsed_ms >= STEP_INIT_MS;                                                    // 判断步时是否超时
                    const bool total_expired = cur_total_left_ms <= elapsed_ms;                                              // 判断局时是否超时

                    if (step_expired || total_expired)
                    {
                        const uint64_t timed_out_uid = cur_uid;
                        const uint64_t winner_id = (timed_out_uid == _white_id) ? _black_id : _white_id;
                        const uint64_t loser_id = timed_out_uid;

                        json_resp["optype"] = "timeout";
                        json_resp["result"] = true;
                        json_resp["reason"] = step_expired ? "步时超时" : "局时超时";
                        json_resp["room_id"] = (Json::UInt64)_room_id;
                        json_resp["uid"] = (Json::UInt64)timed_out_uid;
                        json_resp["winner"] = (Json::UInt64)winner_id;

                        _tb_user->win(winner_id);
                        _tb_user->lose(loser_id);
                        _statu = GAME_OVER;
                        broadcast(json_resp);
                        return;
                    }
                }

                // 执行落子与五连判断
                json_resp = handle_chess(req);

                // 如果落子合法（result=true），再扣减计时并切换轮次
                if (json_resp["result"].asBool())
                {
                    // 第一次合法落子后开始计时
                    if (!_timing_started && json_resp["winner"].asUInt64() == 0)
                    {
                        _timing_started = true;
                        _white_total_left_ms = TOTAL_INIT_MS;
                        _black_total_left_ms = TOTAL_INIT_MS;

                        // 计时从“第一步落子后”的下一位玩家开始思考
                        _turn_uid = (uid == _white_id) ? _black_id : _white_id;
                        _turn_start_tp = std::chrono::steady_clock::now();

                        json_resp["timing_started"] = true;
                        json_resp["total_left_white_ms"] = (Json::UInt64)_white_total_left_ms;
                        json_resp["total_left_black_ms"] = (Json::UInt64)_black_total_left_ms;
                        json_resp["step_init_ms"] = (Json::UInt64)STEP_INIT_MS;
                    }
                    else if (_timing_started && json_resp["winner"].asUInt64() == 0)
                    {
                        // 扣减当前轮到玩家的“思考时间”
                        const uint64_t cur_uid = _turn_uid; // 应等于 uid（轮次已校验）
                        uint64_t &cur_total_left_ms = (cur_uid == _white_id) ? _white_total_left_ms : _black_total_left_ms;
                        if (cur_total_left_ms > elapsed_ms)
                            cur_total_left_ms -= elapsed_ms;
                        else
                            cur_total_left_ms = 0;

                        // 下一位轮到的玩家
                        _turn_uid = (uid == _white_id) ? _black_id : _white_id;
                        _turn_start_tp = std::chrono::steady_clock::now();

                        json_resp["timing_started"] = true;
                        json_resp["total_left_white_ms"] = (Json::UInt64)_white_total_left_ms;
                        json_resp["total_left_black_ms"] = (Json::UInt64)_black_total_left_ms;
                        json_resp["step_init_ms"] = (Json::UInt64)STEP_INIT_MS;
                    }
                }

                // 五连胜负处理（无论是否已开始计时）
                if (json_resp["winner"].asUInt64() != 0)
                {
                    const uint64_t winner_id = json_resp["winner"].asUInt64();
                    const uint64_t loser_id = (winner_id == _white_id) ? _black_id : _white_id;
                    _tb_user->win(winner_id);
                    _tb_user->lose(loser_id);
                    _statu = GAME_OVER;

                    // 结束对局时也附带计时字段，便于前端停止计时
                    json_resp["timing_started"] = _timing_started;
                    json_resp["total_left_white_ms"] = (Json::UInt64)_white_total_left_ms;
                    json_resp["total_left_black_ms"] = (Json::UInt64)_black_total_left_ms;
                    json_resp["step_init_ms"] = (Json::UInt64)STEP_INIT_MS;
                }
            }
        }
        else if (optype == "timeout")
        {
            const uint64_t uid = req["uid"].asUInt64();

            if (_statu == GAME_OVER)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "游戏已经结束了！";
            }
            else if (!_timing_started)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "尚未开始计时";
            }
            else if (uid != _turn_uid)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "还没轮到你";
            }
            else
            {
                const auto now_tp = std::chrono::steady_clock::now();
                const uint64_t elapsed_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - _turn_start_tp).count();

                const uint64_t cur_uid = _turn_uid;
                const uint64_t cur_total_left_ms = (cur_uid == _white_id) ? _white_total_left_ms : _black_total_left_ms;
                const bool step_expired = elapsed_ms + TIMEOUT_TOLERANCE_MS >= STEP_INIT_MS;
                const bool total_expired = cur_total_left_ms <= elapsed_ms + TIMEOUT_TOLERANCE_MS;

                if (!step_expired && !total_expired)
                {
                    json_resp["optype"] = optype;
                    json_resp["result"] = false;
                    json_resp["reason"] = "尚未超时";
                }
                else
                {
                    const uint64_t timed_out_uid = cur_uid;
                    const uint64_t winner_id = (timed_out_uid == _white_id) ? _black_id : _white_id;
                    const uint64_t loser_id = timed_out_uid;

                    json_resp["optype"] = "timeout";
                    json_resp["result"] = true;
                    json_resp["reason"] = step_expired ? "步时超时" : "局时超时";
                    json_resp["room_id"] = (Json::UInt64)_room_id;
                    json_resp["uid"] = (Json::UInt64)timed_out_uid;
                    json_resp["winner"] = (Json::UInt64)winner_id;

                    _tb_user->win(winner_id);
                    _tb_user->lose(loser_id);
                    _statu = GAME_OVER;
                }
            }
        }
        else if (optype == "rematch_invite")
        {
            const uint64_t uid = req["uid"].asUInt64();

            if (_statu != GAME_OVER)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "游戏尚未结束，不能发起再来一局";
            }
            else if (uid != _white_id && uid != _black_id)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "只有房间内的玩家可发起再来一局";
            }
            else if (_rematch_initiator_uid != 0)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "已有再来一局邀请尚未处理";
            }
            else if (!both_players_online())
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "双方玩家需要保持在线后才能再来一局";
            }
            else
            {
                _rematch_initiator_uid = uid;
                json_resp["optype"] = optype;
                json_resp["result"] = true;
                json_resp["room_id"] = (Json::UInt64)_room_id;
                json_resp["inviter_uid"] = (Json::UInt64)uid;

                json_resp["white_id"] = (Json::UInt64)_white_id;
                json_resp["black_id"] = (Json::UInt64)_black_id;
                fill_white_black_usernames(json_resp);
            }
        }
        else if (optype == "rematch_accept")
        {
            const uint64_t uid = req["uid"].asUInt64();

            if (_statu != GAME_OVER)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "游戏尚未结束，不能同意再来一局";
            }
            else if (_rematch_initiator_uid == 0)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "当前没有再来一局邀请";
            }
            else if (uid != _white_id && uid != _black_id)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "只有房间内的玩家可同意再来一局";
            }
            else if (uid == _rematch_initiator_uid)
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "不能同意自己的邀请";
            }
            else if (!both_players_online())
            {
                json_resp["optype"] = optype;
                json_resp["result"] = false;
                json_resp["reason"] = "双方玩家需要保持在线后才能再来一局";
            }
            else
            {
                // 同意成功：交换白/黑并广播 rematch_start
                json_resp = start_rematch();
            }
        }
        else if (optype == "chat")
        {
            json_resp = handle_chat(req);
        }
        else
        {
            json_resp["optype"] = optype;
            json_resp["result"] = false;
            json_resp["reason"] = "未知请求类型";
        }
        broadcast(json_resp);
    }
};

using room_ptr = std::shared_ptr<room>;

class room_manager
{
private:
    uint64_t _next_rid = 1; // 下一个房间 ID，单调递增
    std::mutex _mutex;
    user_table *_tb_user = nullptr;                // 数据库访问接口
    online_manager *_online_user = nullptr;        // 在线用户管理接口
    std::unordered_map<uint64_t, room_ptr> _rooms; // rid -> 房间指针
    std::unordered_map<uint64_t, uint64_t> _users; // 用户 uid -> 房间 rid

public:
    room_manager(user_table *ut, online_manager *om) : _tb_user(ut), _online_user(om)
    {
        DBG_LOG("房间管理模块初始化完毕！");
    }

    ~room_manager()
    {
        DBG_LOG("房间管理模块即将销毁！");
    }
    // 创建房间：成功返回房间指针，失败返回空指针
    room_ptr create_room(uint64_t uid1, uint64_t uid2)
    {
        // 创建房间前先检查双方是否都在大厅中（且连接仍在线），如果不满足条件则创建失败
        if (!_online_user->in_game_hall(uid1))
        {
            DBG_LOG("用户：%lu 不在大厅中，创建房间失败!", uid1);
            return room_ptr();
        }
        if (!_online_user->in_game_hall(uid2))
        {
            DBG_LOG("用户：%lu 不在大厅中，创建房间失败!", uid2);
            return room_ptr();
        }

        // 创建房间，并把双方用户添加到房间中
        std::unique_lock<std::mutex> lock(_mutex);
        room_ptr rp(new room(_next_rid, _tb_user, _online_user));
        rp->add_white_user(uid1);
        rp->add_black_user(uid2);

        // 把新创建的房间添加到房间管理器中，并建立用户 uid -> 房间 rid 的映射关系
        _rooms.insert(std::make_pair(_next_rid, rp));
        _users.insert(std::make_pair(uid1, _next_rid));
        _users.insert(std::make_pair(uid2, _next_rid));
        _next_rid++;
        return rp;
    }

    // 根据房间 ID 获取房间指针：成功返回房间指针，失败返回空指针
    room_ptr get_room_by_rid(uint64_t rid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _rooms.find(rid);
        if (it == _rooms.end())
            return room_ptr();
        return it->second;
    }

    // 根据用户 ID 获取所在房间的房间指针：成功返回房间指针，失败返回空指针
    room_ptr get_room_by_uid(uint64_t uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto uit = _users.find(uid);
        if (uit == _users.end())
            return room_ptr();

        const uint64_t rid = uit->second;
        auto rit = _rooms.find(rid);
        if (rit == _rooms.end())
            return room_ptr();
        return rit->second;
    }

    // 删除房间：当房间中没有玩家了或者游戏结束时调用
    void remove_room(uint64_t rid)
    {
        room_ptr rp = get_room_by_rid(rid);
        if (rp.get() == nullptr)
            return;

        const uint64_t uid1 = rp->get_white_user();
        const uint64_t uid2 = rp->get_black_user();

        std::unique_lock<std::mutex> lock(_mutex);
        _users.erase(uid1);
        _users.erase(uid2);
        _rooms.erase(rid);
    }

    // 删除房间中指定用户：当房间中没有玩家了则销毁房间
    void remove_room_user(uint64_t uid)
    {
        room_ptr rp = get_room_by_uid(uid); // 获取用户所在的房间
        if (rp.get() == nullptr)
            return;

        rp->handle_exit(uid);        // 处理用户退出房间的相关逻辑
        if (rp->player_count() == 0) // 如果房间中没有玩家了则销毁房间
            remove_room(rp->id());
    }

    // 移除观战者：不会调用 room::handle_exit，不影响胜负
    void remove_spectator_user(uint64_t uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        for (auto &kv : _rooms)
        {
            if (kv.second.get() != nullptr)
                kv.second->remove_spectator(uid);
        }
    }

    // 获取指定状态的房间列表
    std::vector<room_ptr> get_rooms_by_state(room_statu st)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::vector<room_ptr> out;
        for (const auto &kv : _rooms)
        {
            if (kv.second.get() != nullptr && kv.second->statu() == st)
                out.push_back(kv.second);
        }
        return out;
    }
};

#endif
