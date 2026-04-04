#ifndef M_SRV_H
#define M_SRV_H

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> wsserver_t;
typedef websocketpp::server<websocketpp::config::asio>::message_ptr message_ptr;
typedef websocketpp::server<websocketpp::config::asio>::connection_ptr connection_ptr;
typedef websocketpp::connection_hdl connection_hdl;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::steady_timer> timer_ptr;

#include "db.hpp"
#include "match.hpp"
#include "online.hpp"
#include "room.hpp"
#include "session.hpp"
#include "util.hpp"

#include <cstdint>
#include <string>

#ifndef WWWROOT
#define WWWROOT "./wwwroot/"
#endif

// 五子棋服务器（HTTP + WebSocket）
class gobang_server
{
private:
    std::string _web_root; // 静态资源根目录
    wsserver_t _wssrv;     // websocketpp server endpoint

    user_table _ut;      // 用户表
    online_manager _om;  // 在线管理器
    room_manager _rm;    // 房间管理器
    matcher _mm;         // 匹配器
    session_manager _sm; // 会话管理器

private:
    // 统一 HTTP JSON 响应
    void http_resp(wsserver_t::connection_ptr &conn,
                   bool result,
                   websocketpp::http::status_code::value code,
                   const std::string &reason)
    {
        Json::Value resp_json;
        resp_json["result"] = result;
        resp_json["reason"] = reason;

        std::string resp_body;
        json_util::serialize(resp_json, resp_body);

        conn->set_status(code);
        conn->set_body(resp_body);
        conn->append_header("Content-Type", "application/json");
    }

    // 静态资源请求处理
    void file_handler(wsserver_t::connection_ptr &conn)
    {
        websocketpp::http::parser::request req = conn->get_request(); // 获取请求
        std::string uri = req.get_uri();                              // 获取 uri（可能包含查询字符串）
        const size_t qpos = uri.find('?');
        if (qpos != std::string::npos)
            uri = uri.substr(0, qpos); // 静态资源只认路径部分，忽略 ?query

        std::string realpath = _web_root + uri; // 获取真实路径
        if (!realpath.empty() && realpath.back() == '/')
            realpath += "login.html"; // 如果路径结尾是/，则添加login.html(默认页面)

        std::string body; // 读取文件内容
        if (!file_util::read(realpath, body))
        {
            std::string not_found; // 未找到
            not_found += "<html><head><meta charset='UTF-8'/></head><body><h1> Not Found </h1></body></html>";
            conn->set_status(websocketpp::http::status_code::not_found);
            conn->set_body(not_found); // 设置响应体
            return;
        }

        conn->set_body(body);
        conn->set_status(websocketpp::http::status_code::ok);
    }

    // 用户注册
    void reg(wsserver_t::connection_ptr &conn)
    {
        std::string req_body = conn->get_request_body();   // 获取请求体
        Json::Value login_info;                            // 解析请求体
        if (!json_util::unserialize(req_body, login_info)) // 反序列化请求体
        {
            DBG_LOG("反序列化注册信息失败");
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "请求正文格式错误"); // 返回错误响应
        }

        if (login_info["username"].isNull() || login_info["password"].isNull()) // 用户名或密码为空
        {
            DBG_LOG("用户名密码不完整");
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "请输入用户名/密码"); // 返回错误响应
        }

        bool ret = _ut.insert(login_info); // 插入用户信息
        if (!ret)
        {
            DBG_LOG("向数据库插入数据失败"); // 插入失败
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "用户名已经被占用");
        }

        return http_resp(conn, true, websocketpp::http::status_code::ok, "注册用户成功"); // 返回成功响应
    }

    // 用户登录
    void login(wsserver_t::connection_ptr &conn)
    {
        std::string req_body = conn->get_request_body();
        Json::Value login_info;                            // 解析请求体
        if (!json_util::unserialize(req_body, login_info)) // 反序列化请求体
        {
            DBG_LOG("反序列化登录信息失败");
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "请求正文格式错误"); // 返回错误响应
        }

        if (login_info["username"].isNull() || login_info["password"].isNull()) // 用户名或密码为空
        {
            DBG_LOG("用户名密码不完整");
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "请输入用户名/密码"); // 返回错误响应
        }

        bool ret = _ut.login(login_info); // 登录验证
        if (!ret)
        {
            DBG_LOG("用户名或密码错误");
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "用户名或密码错误");
        }

        uint64_t uid = login_info["id"].asUInt64();       // 获取用户ID
        session_ptr ssp = _sm.create_session(uid, LOGIN); // 创建会话
        if (!ssp)
        {
            DBG_LOG("创建会话失败"); // 创建失败
            return http_resp(conn, false, websocketpp::http::status_code::internal_server_error, "创建会话失败");
        }

        _sm.set_session_expire_time(ssp->ssid(), SESSION_TIMEOUT); // 设置会话超时时间

        std::string cookie_ssid = "SSID=" + std::to_string(ssp->ssid()); // 设置cookie
        conn->append_header("Set-Cookie", cookie_ssid);
        return http_resp(conn, true, websocketpp::http::status_code::ok, "登录成功"); // 返回成功响应
    }

    // Cookie: SSID=XXX; path=/;
    bool get_cookie_val(const std::string &cookie_str, const std::string &key, std::string &val)
    {
        std::vector<std::string> cookie_arr; // 分割cookie
        string_util::split(cookie_str, "; ", cookie_arr);
        for (const auto &str : cookie_arr)
        {
            std::vector<std::string> kv;
            string_util::split(str, "=", kv); // 分割key=value
            if (kv.size() != 2)
                continue;
            if (kv[0] == key) // 判断key是否匹配
            {
                val = kv[1]; // 获取value
                return true;
            }
        }
        return false; // 匹配失败
    }

    // 从 Cookie 获取 session
    session_ptr get_session_by_cookie(wsserver_t::connection_ptr &conn, const std::string &optype_for_err)
    {
        Json::Value err_resp;
        std::string cookie_str = conn->get_request_header("Cookie"); // 获取cookie
        if (cookie_str.empty())                                      // cookie为空
        {
            err_resp["optype"] = optype_for_err;
            err_resp["reason"] = "没有找到cookie信息,需要重新登录";
            err_resp["result"] = false;
            ws_resp(conn, err_resp);
            return session_ptr();
        }

        std::string ssid_str;
        if (!get_cookie_val(cookie_str, "SSID", ssid_str)) // 获取SSID
        {
            err_resp["optype"] = optype_for_err;
            err_resp["reason"] = "没有找到SSID信息,需要重新登录";
            err_resp["result"] = false; // 设置响应结果为false
            ws_resp(conn, err_resp);    // 发送响应
            return session_ptr();       // 返回空指针
        }

        session_ptr ssp = _sm.get_session_by_ssid(std::stoull(ssid_str)); // 根据SSID获取session
        if (!ssp)                                                         // 如果session为空
        {
            err_resp["optype"] = optype_for_err;
            err_resp["reason"] = "没有找到session信息,需要重新登录";
            err_resp["result"] = false; // 设置响应结果为false
            ws_resp(conn, err_resp);    // 发送响应
            return session_ptr();       // 返回空指针
        }

        return ssp; // 返回session
    }

    // 用户信息接口（HTTP）
    void info(wsserver_t::connection_ptr &conn)
    {
        std::string cookie_str = conn->get_request_header("Cookie"); // 获取cookie
        if (cookie_str.empty())
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "找不到cookie信息,请重新登录");

        std::string ssid_str;
        if (!get_cookie_val(cookie_str, "SSID", ssid_str)) // 获取SSID
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "找不到ssid信息,请重新登录");

        session_ptr ssp = _sm.get_session_by_ssid(std::stoull(ssid_str));                                      // 根据SSID获取session
        if (!ssp)                                                                                              // 如果session为空
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "登录过期,请重新登录"); // 登录过期

        Json::Value user_info;                 // 解析用户信息
        uint64_t uid = ssp->get_user();        // 获取用户ID
        if (!_ut.select_by_id(uid, user_info)) // 获取用户信息
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "找不到用户信息,请重新登录");

        std::string body;                                        // 序列化用户信息
        json_util::serialize(user_info, body);                   // 序列化用户信息
        conn->set_body(body);                                    // 设置响应体
        conn->append_header("Content-Type", "application/json"); // 设置响应头
        conn->set_status(websocketpp::http::status_code::ok);

        _sm.set_session_expire_time(ssp->ssid(), SESSION_TIMEOUT); // 滑动续期（用户有活跃请求，就把会话过期时间往后延）
    }

    // HTTP 回调
    void http_callback(websocketpp::connection_hdl hdl)
    {
        wsserver_t::connection_ptr conn = _wssrv.get_con_from_hdl(hdl); // 获取连接
        websocketpp::http::parser::request req = conn->get_request();   // 获取请求
        std::string method = req.get_method();                          // 获取请求方法（GET/POST）
        std::string uri = req.get_uri();                                // 获取URI

        if (method == "POST" && uri == "/reg") // 判断方法和URI是否匹配
            return reg(conn);
        if (method == "POST" && uri == "/login") // 判断方法和URI是否匹配
            return login(conn);
        if (method == "GET" && uri == "/info") // 判断方法和URI是否匹配
            return info(conn);
        return file_handler(conn); // 处理文件请求
        // 注：函数调用结束后，响应自动发送给客户端
    }

    // WebSocket 发送 JSON
    void ws_resp(wsserver_t::connection_ptr &conn, Json::Value &resp)
    {
        std::string body; // 序列化响应
        json_util::serialize(resp, body);
        conn->send(body); // 发送响应
    }

    // /hall 建连
    void wsopen_game_hall(wsserver_t::connection_ptr &conn)
    {
        // DBG_LOG("进入wsopen_game_hall函数，开始处理大厅连接");
        //  游戏大厅长连接建立成功
        Json::Value resp_json;
        // 1.登录验证--判断当前客户端是否已经成功登录
        session_ptr ssp = get_session_by_cookie(conn, "hall_ready"); // 获取会话
        if (!ssp)
            return;
        // 2. 判断当前客户端是否是重复登录（同一用户ID在游戏大厅或房间中已经有在线连接了）
        uint64_t uid = ssp->get_user(); // 获取用户ID 是否在游戏大厅或房间（是否在线）
        if (_om.in_game_hall(uid) || _om.in_game_room(uid))
        {
            resp_json["optype"] = "hall_ready";     // 设置操作类型
            resp_json["reason"] = "玩家重复登录！"; // 设置原因
            resp_json["result"] = false;
            return ws_resp(conn, resp_json);
        }
        // 3. 将当前客户端以及连接加入到游戏大厅
        _om.enter_game_hall(uid, conn); // 进入游戏大厅
        // 4. 给客户端响应游戏大厅连接建立成功
        resp_json["optype"] = "hall_ready"; // 设置操作类型
        resp_json["result"] = true;         // 设置结果为true
        ws_resp(conn, resp_json);
        // 5. 设置会话超时时间为永久存在（进入大厅后不希望会话过期被删除）
        _sm.set_session_expire_time(ssp->ssid(), SESSION_FOREVER); // 设置会话超时时间
    }

    // /room 建连
    void wsopen_game_room(wsserver_t::connection_ptr &conn)
    {
        DBG_LOG("进入wsopen_game_room函数，开始处理房间连接");
        Json::Value resp_json;
        // 1.登录验证--判断当前客户端是否已经成功登录
        session_ptr ssp = get_session_by_cookie(conn, "room_ready"); // 获取会话
        if (!ssp)
        {
            DBG_LOG("wsopen_game_room: session为空，直接返回");
            return;
        }
        // 2. 判断当前客户端是否是重复登录（同一用户ID在游戏大厅或房间中已经有在线连接了）
        uint64_t uid = ssp->get_user(); // 获取用户ID 是否在游戏大厅或房间（是否在线）
        bool in_room = _om.in_game_room(uid);
        bool in_hall = _om.in_game_hall(uid);
        DBG_LOG("wsopen_game_room: uid=%llu in_hall=%d in_room=%d", (unsigned long long)uid, in_hall, in_room);

        if (in_room)
        {
            DBG_LOG("wsopen_game_room: 命中重复登录（在房间内），uid=%llu", (unsigned long long)uid);
            resp_json["optype"] = "room_ready"; // 设置操作类型
            resp_json["reason"] = "玩家重复登录！";
            resp_json["result"] = false;
            return ws_resp(conn, resp_json);
        }
        if (in_hall)
        {
            DBG_LOG("wsopen_game_room: uid=%llu 仍在大厅，先退出大厅映射后进入房间", (unsigned long long)uid);
            _om.exit_game_hall(uid);
        }
        // 3. 获取房间信息（比如房间ID、对手信息等），如果没有找到房间信息则返回错误响应
        room_ptr rp = _rm.get_room_by_uid(uid); // 获取房间
        if (!rp)
        {
            DBG_LOG("wsopen_game_room: uid=%llu 找不到房间信息", (unsigned long long)uid);
            resp_json["optype"] = "room_ready"; // 设置操作类型
            resp_json["reason"] = "没有找到玩家的房间信息";
            resp_json["result"] = false;
            return ws_resp(conn, resp_json);
        }
        // DBG_LOG("wsopen_game_room: uid=%llu room_id=%llu white_id=%llu black_id=%llu",
        //         (unsigned long long)uid,
        //         (unsigned long long)rp->id(),
        //         (unsigned long long)rp->get_white_user(),
        //         (unsigned long long)rp->get_black_user());
        // 4. 将当前客户端以及连接加入到游戏房间
        _om.enter_game_room(uid, conn); // 进入游戏房间
        DBG_LOG("wsopen_game_room: uid=%llu 已进入游戏房间映射", (unsigned long long)uid);
        // 5. 设置会话超时时间为永久存在（进入房间后不希望会话过期被删除）
        _sm.set_session_expire_time(ssp->ssid(), SESSION_FOREVER); // 设置会话超时时间
        // 6. 回复房间准备完毕
        resp_json["optype"] = "room_ready";                         // 设置操作类型
        resp_json["result"] = true;                                 // 设置结果为true
        resp_json["room_id"] = (Json::UInt64)rp->id();              // 设置房间ID
        resp_json["uid"] = (Json::UInt64)uid;                       // 设置用户ID
        resp_json["white_id"] = (Json::UInt64)rp->get_white_user(); // 设置白棋用户ID
        resp_json["black_id"] = (Json::UInt64)rp->get_black_user(); // 设置黑棋用户ID

        // 观战/展示用：补充白/黑用户名（前端不希望拿 uid 直接显示）
        Json::Value u;
        if (_ut.select_by_id(rp->get_white_user(), u) && !u["username"].isNull())
            resp_json["white_username"] = u["username"];
        else
            resp_json["white_username"] = "";

        if (_ut.select_by_id(rp->get_black_user(), u) && !u["username"].isNull())
            resp_json["black_username"] = u["username"];
        else
            resp_json["black_username"] = "";
        // 计时信息：计时在第一步合法落子后才真正开始
        resp_json["timing_started"] = false;
        resp_json["total_init_ms"] = (Json::UInt64)600000; // 600s
        resp_json["step_init_ms"] = (Json::UInt64)30000;   // 30s
        ws_resp(conn, resp_json);                          // 发送响应
    }

    // /spectate 建连
    void wsopen_spectate(wsserver_t::connection_ptr &conn)
    {
        // 观战连接建立后不立即加入房间，等待客户端发送 spectate_join
        session_ptr ssp = get_session_by_cookie(conn, "spectate_ready");
        if (!ssp)
            return;

        // 观战期间不希望 session 立刻过期
        _sm.set_session_expire_time(ssp->ssid(), SESSION_FOREVER);
    }

    void wsopen_callback(websocketpp::connection_hdl hdl)
    {
        // DBG_LOG("进入wsopen_callback函数，开始处理WebSocket长连接建立成功事件");
        // WebSocket 长连接建立成功的回调函数
        wsserver_t::connection_ptr conn = _wssrv.get_con_from_hdl(hdl); // 获取连接
        std::string uri = conn->get_request().get_uri();                // 获取URI
        if (uri == "/hall")
            return wsopen_game_hall(conn); // 建立了游戏大厅的长连接
        else if (uri == "/room")
            return wsopen_game_room(conn); // 建立了游戏房间的长连接
        else if (uri == "/spectate")
            return wsopen_spectate(conn); // 建立了游戏观战连接
    }

    // /hall 断连
    void wsclose_game_hall(wsserver_t::connection_ptr &conn)
    {
        // 游戏大厅长连接断开的处理
        // 1.  登录验证--判断当前客⼾端是否已经成功登录
        session_ptr ssp = get_session_by_cookie(conn, "hall_ready"); // 获取会话
        if (!ssp)
            return;
        // 2. 将玩家从游戏大厅中移除
        _om.exit_game_hall(ssp->get_user());
        // 3. 将session恢复生命周期的管理，设置定时销毁
        _sm.set_session_expire_time(ssp->ssid(), SESSION_TIMEOUT);
    }

    /// /room 建连断开后的处理
    void wsclose_game_room(wsserver_t::connection_ptr &conn)
    {
        // 游戏房间长连接断开的处理
        // 1.  登录验证--判断当前客户端是否已经成功登录
        session_ptr ssp = get_session_by_cookie(conn, "room_ready"); // 获取会话
        if (!ssp)
            return;
        // 2. 将玩家从游戏房间中移除
        _om.exit_game_room(ssp->get_user());
        // 3. 将session恢复生命周期的管理，设置定时销毁
        _sm.set_session_expire_time(ssp->ssid(), SESSION_TIMEOUT);
        // 4. 将玩家从房间管理器中移除（比如玩家掉线了，房间需要解散或者等待其他玩家加入等）
        _rm.remove_room_user(ssp->get_user());
    }

    void wsclose_callback(websocketpp::connection_hdl hdl)
    {
        wsserver_t::connection_ptr conn = _wssrv.get_con_from_hdl(hdl);
        std::string uri = conn->get_request().get_uri(); // 获取URI
        if (uri == "/hall")                              // 判断URI是否匹配
            return wsclose_game_hall(conn);
        if (uri == "/room") // 判断URI是否匹配
            return wsclose_game_room(conn);
        if (uri == "/spectate")
            wsclose_spectate(conn);
    }

    // /spectate 断连
    void wsclose_spectate(wsserver_t::connection_ptr &conn)
    {
        session_ptr ssp = get_session_by_cookie(conn, "spectate_ready");
        if (!ssp)
            return;

        const uint64_t uid = ssp->get_user();
        _rm.remove_spectator_user(uid);
        _sm.set_session_expire_time(ssp->ssid(), SESSION_TIMEOUT);
    }

    // /hall 消息处理
    void wsmsg_game_hall(wsserver_t::connection_ptr &conn, wsserver_t::message_ptr msg)
    {
        // DBG_LOG("进入wsmsg_game_hall函数，开始处理大厅消息");
        Json::Value resp_json;
        session_ptr ssp = get_session_by_cookie(conn, "hall_ready"); // 获取会话   （是否在游戏大厅）
        if (!ssp)
        {
            DBG_LOG("wsmsg_game_hall: session为空，直接返回");
            return;
        }

        std::string req_body = msg->get_payload(); // 获取请求体
        Json::Value req_json;
        if (!json_util::unserialize(req_body, req_json)) // 反序列化请求体
        {
            DBG_LOG("wsmsg_game_hall: 请求信息解析失败，直接返回");
            resp_json["result"] = false;              // 设置结果为false
            resp_json["reason"] = "请求信息解析失败"; // 设置原因
            return ws_resp(conn, resp_json);
        }

        const std::string optype = req_json["optype"].asString(); // 获取操作类型
        if (optype == "match_start")                              // 匹配开始
        {
            DBG_LOG("wsmsg_game_hall: 匹配开始，添加用户: %llu", (unsigned long long)ssp->get_user());
            _mm.add(ssp->get_user());            // 添加用户
            resp_json["optype"] = "match_start"; // 设置操作类型
            resp_json["result"] = true;          // 设置结果为true
            return ws_resp(conn, resp_json);
        }
        if (optype == "match_stop") // 匹配结束
        {
            DBG_LOG("wsmsg_game_hall: 匹配结束，删除用户: %llu", (unsigned long long)ssp->get_user());
            _mm.del(ssp->get_user());           // 删除用户
            resp_json["optype"] = "match_stop"; // 设置操作类型
            resp_json["result"] = true;         // 设置结果为true
            return ws_resp(conn, resp_json);
        }

        if (optype == "room_list_request") // 房间列表请求
        {
            resp_json["optype"] = "room_list";
            resp_json["result"] = true;

            Json::Value rooms_arr(Json::arrayValue);
            std::vector<room_ptr> rooms = _rm.get_rooms_by_state(GAME_START);
            for (auto &rp : rooms)
            {
                Json::Value one;
                one["room_id"] = (Json::UInt64)rp->id();
                one["white_id"] = (Json::UInt64)rp->get_white_user();
                one["black_id"] = (Json::UInt64)rp->get_black_user();
                one["statu"] = (int)rp->statu();

                // 用于大厅显示：补充白/黑用户名（缺失时回退为 id）
                Json::Value u;
                if (_ut.select_by_id(rp->get_white_user(), u) && !u["username"].isNull())
                    one["white_username"] = u["username"];
                else
                    one["white_username"] = (Json::UInt64)rp->get_white_user();

                if (_ut.select_by_id(rp->get_black_user(), u) && !u["username"].isNull())
                    one["black_username"] = u["username"];
                else
                    one["black_username"] = (Json::UInt64)rp->get_black_user();

                rooms_arr.append(one);
            }
            resp_json["rooms"] = rooms_arr;
            return ws_resp(conn, resp_json);
        }

        DBG_LOG("wsmsg_game_hall: 未知请求类型，直接返回");
        resp_json["optype"] = "unknown";      // 设置操作类型   （未知请求类型）
        resp_json["reason"] = "请求类型未知"; // 设置原因
        resp_json["result"] = false;          // 设置结果为false
        ws_resp(conn, resp_json);             // 发送响应
    }

    // /room 消息处理
    void wsmsg_game_room(wsserver_t::connection_ptr &conn, wsserver_t::message_ptr msg)
    {
        // DBG_LOG("进入wsmsg_game_room函数，开始处理房间消息");
        Json::Value resp_json;
        session_ptr ssp = get_session_by_cookie(conn, "room_ready"); // 获取会话   （是否在游戏房间）
        if (!ssp)
            return;
        room_ptr rp = _rm.get_room_by_uid(ssp->get_user()); // 获取房间
        if (!rp)
        {
            resp_json["optype"] = "unknown";                // 设置操作类型   （没有找到玩家的房间信息）
            resp_json["reason"] = "没有找到玩家的房间信息"; // 设置原因
            resp_json["result"] = false;                    // 设置结果为false
            return ws_resp(conn, resp_json);
        }

        Json::Value req_json;
        std::string req_body = msg->get_payload();       // 获取请求体
        if (!json_util::unserialize(req_body, req_json)) // 反序列化请求体
        {
            resp_json["optype"] = "unknown";      // 设置操作类型
            resp_json["reason"] = "请求解析失败"; // 设置原因
            resp_json["result"] = false;          // 设置结果为false
            return ws_resp(conn, resp_json);
        }

        rp->handle_request(req_json); // 处理请求
    }

    // /spectate 消息处理
    void wsmsg_spectate(wsserver_t::connection_ptr &conn, wsserver_t::message_ptr msg)
    {
        Json::Value resp_json;
        session_ptr ssp = get_session_by_cookie(conn, "spectate_ready");
        if (!ssp)
            return;

        const uint64_t uid = ssp->get_user();

        Json::Value req_json;
        std::string req_body = msg->get_payload();
        if (!json_util::unserialize(req_body, req_json))
        {
            resp_json["optype"] = "spectate_ready";
            resp_json["result"] = false;
            resp_json["reason"] = "请求解析失败";
            return ws_resp(conn, resp_json);
        }

        const std::string optype = req_json["optype"].asString();
        if (optype == "spectate_join")
        {
            const uint64_t rid = req_json["room_id"].asUInt64();
            room_ptr rp = _rm.get_room_by_rid(rid);
            if (!rp)
            {
                resp_json["optype"] = "spectate_ready";
                resp_json["result"] = false;
                resp_json["reason"] = "房间不存在";
                return ws_resp(conn, resp_json);
            }

            // 仅允许观战进行中的对局
            if (rp->statu() != GAME_START)
            {
                resp_json["optype"] = "spectate_ready";
                resp_json["result"] = false;
                resp_json["reason"] = "房间未在进行中";
                return ws_resp(conn, resp_json);
            }

            // 一个 uid 同时只观战一个房间：先从所有房间移除
            _rm.remove_spectator_user(uid);
            rp->add_spectator(uid, conn);

            resp_json["optype"] = "spectate_ready";
            resp_json["result"] = true;
            resp_json["room_id"] = (Json::UInt64)rp->id();
            resp_json["white_id"] = (Json::UInt64)rp->get_white_user();
            resp_json["black_id"] = (Json::UInt64)rp->get_black_user();
            resp_json["statu"] = (int)rp->statu();
            resp_json["board_snapshot"] = rp->board_snapshot();

            // 观战展示用：补充白/黑用户名
            Json::Value u;
            if (_ut.select_by_id(rp->get_white_user(), u) && !u["username"].isNull())
                resp_json["white_username"] = u["username"];
            else
                resp_json["white_username"] = "";

            if (_ut.select_by_id(rp->get_black_user(), u) && !u["username"].isNull())
                resp_json["black_username"] = u["username"];
            else
                resp_json["black_username"] = "";
            return ws_resp(conn, resp_json);
        }

        resp_json["optype"] = optype;
        resp_json["result"] = false;
        resp_json["reason"] = "未知观战请求类型";
        ws_resp(conn, resp_json);
    }

    // WebSocket 消息处理
    void wsmsg_callback(websocketpp::connection_hdl hdl, wsserver_t::message_ptr msg)
    {
        wsserver_t::connection_ptr conn = _wssrv.get_con_from_hdl(hdl); // 获取连接
        std::string uri = conn->get_request().get_uri();                // 获取URI
        if (uri == "/hall")                                             // 判断URI是否匹配
            return wsmsg_game_hall(conn, msg);                          // 处理游戏大厅消息    （是否在游戏大厅）
        if (uri == "/room")                                             // 判断URI是否匹配    （是否在游戏房间）
            return wsmsg_game_room(conn, msg);                          // 处理游戏房间消息    （是否在游戏房间）
        if (uri == "/spectate")
            return wsmsg_spectate(conn, msg);
    }

public:
    gobang_server(const std::string &host,
                  const std::string &user,
                  const std::string &pass,
                  const std::string &dbname,
                  uint16_t port = 3306,
                  const std::string &wwwroot = WWWROOT)
        : _web_root(wwwroot),
          _ut(host, user, pass, dbname, port),
          _rm(&_ut, &_om), //_om(online_manager)有默认构造函数
          _mm(&_rm, &_ut, &_om),
          _sm(&_wssrv)
    // 先按声明顺序依次初始化成员：_web_root -> _wssrv -> _ut -> _om -> _rm -> _mm -> _sm
    {
        _wssrv.set_access_channels(websocketpp::log::alevel::none); // 关闭日志
        _wssrv.init_asio();                                         // 初始化asio
        _wssrv.set_reuse_addr(true);                                // 设置reuse_addr为true（打开端口复用）
        // 注册/登录/用户信息 JSON，以及静态页
        _wssrv.set_http_handler(std::bind(&gobang_server::http_callback, this, std::placeholders::_1)); // 设置http回调
        // WebSocket 握手成功：/hall 进大厅，/room 进房间
        _wssrv.set_open_handler(std::bind(&gobang_server::wsopen_callback, this, std::placeholders::_1)); // 设置wsopen回调
        // 连接断开：清理在线表、会话超时、房间逻辑
        _wssrv.set_close_handler(std::bind(&gobang_server::wsclose_callback, this, std::placeholders::_1)); // 设置wsclose回调
        // 大厅匹配消息、房间内下棋/聊天
        _wssrv.set_message_handler(
            std::bind(&gobang_server::wsmsg_callback, this, std::placeholders::_1, std::placeholders::_2)); // 设置wsmsg回调
    }

    void start(int port)
    {
        _wssrv.listen(port);
        _wssrv.start_accept();
        _wssrv.run();
    }
};

#endif
