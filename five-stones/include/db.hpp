#ifndef __M_DB_H__
#define __M_DB_H__

#include "util.hpp"
#include "cache/user_cache.hpp"

#include <cassert>
#include <cstdint>
#include <mutex>

// 数据库访问接口：提供用户信息的增删改查等功能
class user_table
{
private:
    MYSQL *_mysql;     // mysql操作句柄
    std::mutex _mutex; // 互斥锁保护数据库的访问操作
    UserCache *_user_cache = nullptr;
    int _user_cache_ttl_sec = 120;

public:
    user_table(const std::string &host,
                const std::string &username,
                const std::string &password,
                const std::string &dbname,
                uint16_t port = 3306)
    {
        _mysql = mysql_util::mysql_create(host, username, password, dbname, port);
        assert(_mysql != NULL);
    }

    ~user_table()
    {
        mysql_util::mysql_release(_mysql);
        _mysql = NULL;
    }

    void set_user_cache(UserCache *uc, int ttl_sec = 120)
    {
        _user_cache = uc;
        _user_cache_ttl_sec = ttl_sec;
    }

    // 注册时新增用户
    bool insert(Json::Value &user)
    {
        #define INSERT_USER "insert user values(null, '%s', '%s', 1000, 0, 0);"

        if (user["password"].isNull() || user["username"].isNull())// 用户名或密码不能为空
        {
            DBG_LOG("login/register: username or password empty");
            return false;
        }

        char sql[4096] = {0};
        sprintf(sql, INSERT_USER, user["username"].asCString(), user["password"].asCString());

        std::unique_lock<std::mutex> lock(_mutex);
        bool ret = mysql_util::mysql_exec(_mysql, sql);
        if (ret == false)
        {
            DBG_LOG("insert user info failed!!\n");
            return false;
        }

        return true;
    }

    // 登录验证,并返回详细的用户信息
    bool login(Json::Value &user)
    {
        if (user["password"].isNull() || user["username"].isNull())// 用户名或密码不能为空
        {
            DBG_LOG("login/register: username or password empty");
            return false;
        }

        #define LOGIN_USER "select id, score, total_count, win_count from user where username='%s' and password='%s' order by id desc limit 1;"

        char sql[4096] = {0};
        sprintf(sql, LOGIN_USER, user["username"].asCString(), user["password"].asCString());

        MYSQL_RES *res = NULL;
        {
            std::unique_lock<std::mutex> lock(_mutex);// 加锁保护数据库访问

            bool ret = mysql_util::mysql_exec(_mysql, sql);
            if (ret == false)
            {
                DBG_LOG("user login failed!!\n");
                return false;
            }

            // 按理说要么有数据，要么没有数据；如果有数据通常也只有一条
            res = mysql_store_result(_mysql);// 获取查询结果
            if (res == NULL)
            {
                DBG_LOG("have no login user info!!");
                return false;
            }

            //自动释放锁，离开作用域后会自动调用lock的析构函数释放锁
        }

        int row_num = mysql_num_rows(res);
        if (row_num < 1)
        {
            DBG_LOG("login query returned empty result");
            mysql_free_result(res);
            return false;
        }

        // 获取查询结果的第一行数据（查询结果的列有固定的顺序，按顺序取值）
        MYSQL_ROW row = mysql_fetch_row(res);
        user["id"] = (Json::UInt64)std::stol(row[0]);
        user["score"] = (Json::UInt64)std::stol(row[1]);
        user["total_count"] = std::stoi(row[2]);
        user["win_count"] = std::stoi(row[3]);

        mysql_free_result(res);
        return true;
    }

    // 通过用户名获取用户信息
    bool select_by_name(const std::string &name, Json::Value &user)
    {
        //如果用户缓存不为空，则从用户缓存中获取用户信息
        if (_user_cache && _user_cache->getUserByName(name, user))
            return true;

        //如果用户缓存为空，则从数据库中获取用户信息
        #define USER_BY_NAME "select id, score, total_count, win_count from user where username='%s';"

        char sql[4096] = {0};
        sprintf(sql, USER_BY_NAME, name.c_str());

        //加锁保护数据库访问
        MYSQL_RES *res = NULL;
        {
            std::unique_lock<std::mutex> lock(_mutex);

            //执行SQL语句
            bool ret = mysql_util::mysql_exec(_mysql, sql);
            if (ret == false)
            {
                DBG_LOG("get user by name failed!!\n");
                return false;
            }

            //获取查询结果
            res = mysql_store_result(_mysql);
            if (res == NULL)
            {
                DBG_LOG("have no user info!!");
                return false;
            }
        }

        int row_num = mysql_num_rows(res);
        if (row_num != 1)
        {
            DBG_LOG("the user information queried is not unique!!");
            mysql_free_result(res);
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(res);
        user["id"] = (Json::UInt64)std::stol(row[0]);
        user["username"] = name;
        user["score"] = (Json::UInt64)std::stol(row[1]);
        user["total_count"] = std::stoi(row[2]);
        user["win_count"] = std::stoi(row[3]);

        mysql_free_result(res); //释放查询结果
        //如果用户缓存不为空，则设置用户缓存
        if (_user_cache)
            (void)_user_cache->setUser(user["id"].asUInt64(), name, user, _user_cache_ttl_sec);
        return true;
    }

    // 通过ID获取用户信息
    bool select_by_id(uint64_t id, Json::Value &user)
    {
        //如果用户缓存不为空，则从用户缓存中获取用户信息
        if (_user_cache && _user_cache->getUserById(id, user))
            return true;

        //如果用户缓存为空，则从数据库中获取用户信息
        #define USER_BY_ID "select username, score, total_count, win_count from user where id=%llu;"

        char sql[4096] = {0};
        sprintf(sql, USER_BY_ID, (unsigned long long)id);

        MYSQL_RES *res = NULL;
        {
            std::unique_lock<std::mutex> lock(_mutex);

            bool ret = mysql_util::mysql_exec(_mysql, sql);
            if (ret == false)
            {
                DBG_LOG("get user by id failed!!\n");
                return false;
            }

            res = mysql_store_result(_mysql);
            if (res == NULL)
            {
                DBG_LOG("have no user info!!");
                return false;
            }
        }

        int row_num = mysql_num_rows(res);
        if (row_num != 1)
        {
            DBG_LOG("the user information queried is not unique!!");
            mysql_free_result(res);
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(res);//MYSQL_ROW 本质是 char**（每列一个 char* 字符串）
        user["id"] = (Json::UInt64)id;
        user["username"] = row[0];
        user["score"] = (Json::UInt64)std::stol(row[1]);
        user["total_count"] = std::stoi(row[2]);
        user["win_count"] = std::stoi(row[3]);

        mysql_free_result(res);
        //如果用户缓存不为空，则设置用户缓存
        if (_user_cache)
            (void)_user_cache->setUser(id, user["username"].asString(), user, _user_cache_ttl_sec);
        return true;
    }

    // 胜利：天梯分数 +30，战斗场次 +1，胜利场次 +1
    bool win(uint64_t id)
    {
        #define USER_WIN "update user set score=score+30, total_count=total_count+1, win_count=win_count+1 where id=%llu;"

        char sql[4096] = {0};
        sprintf(sql, USER_WIN, (unsigned long long)id);

        std::unique_lock<std::mutex> lock(_mutex);
        bool ret = mysql_util::mysql_exec(_mysql, sql);
        if (ret == false)
        {
            DBG_LOG("update win user info failed!!\n");
            return false;
        }

        //写数据库成功后，删缓存让下次读取重建，避免读到旧分数。
        //如果用户缓存不为空，则删除用户缓存
        if (_user_cache)
            (void)_user_cache->invalidateById(id);

        return true;
    }

    // 失败：天梯分数 -30，战斗场次 +1，其他不变
    bool lose(uint64_t id)
    {
        #define USER_LOSE "update user set score=score-30, total_count=total_count+1 where id=%llu;"

        char sql[4096] = {0};
        sprintf(sql, USER_LOSE, (unsigned long long)id);

        std::unique_lock<std::mutex> lock(_mutex);
        bool ret = mysql_util::mysql_exec(_mysql, sql);
        if (ret == false)
        {
            DBG_LOG("update lose user info failed!!\n");
            return false;
        }
        //写数据库成功后，删缓存让下次读取重建，避免读到旧分数。
        //如果用户缓存不为空，则删除用户缓存
        if (_user_cache)
            (void)_user_cache->invalidateById(id);

        return true;
    }
};

#endif

