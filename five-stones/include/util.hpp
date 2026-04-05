#pragma once

#include <cctype>
#include <cstdio>
#include <ctime>
#include <pthread.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <mysql/mysql.h>
#include <jsoncpp/json/json.h>
#include "logger.hpp"

namespace {

// 仅取 SQL 首词（如 SELECT/INSERT），不记录完整语句，避免泄露数据。
const char *mysql_sql_kind_token(const std::string &sql)
{
    static char buf[12];
    size_t i = 0;
    while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i])))
        ++i;
    size_t j = 0;
    while (i < sql.size() && j + 1 < sizeof(buf) &&
           std::isalpha(static_cast<unsigned char>(sql[i])))
    {
        buf[j++] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(sql[i++])));
    }
    buf[j] = '\0';
    return j > 0 ? buf : "UNKNOWN";
}

} // namespace

// MySQL-API封装
class mysql_util
{
public:
    static MYSQL *mysql_create(// 连接MySQL服务器
        const std::string &host,
        const std::string &user,
        const std::string &pass,
        const std::string &db,
        int port)
    {
        MYSQL *mysql = mysql_init(NULL);
        if (mysql == NULL)
        {
            ERR_LOG("mysql init failed!");
            return NULL;
        }
        if (mysql_real_connect(mysql, host.c_str(), user.c_str(), pass.c_str(), db.c_str(), port, NULL, 0) == NULL)
        {
            ERR_LOG("mysql conneect server failed! %s", mysql_error(mysql));
            mysql_close(mysql);
            return NULL;
        }
        if (mysql_set_character_set(mysql, "utf8mb4") != 0)
        {
            ERR_LOG("mysql set character failed!");
            mysql_close(mysql);
            return NULL;
        }
        return mysql;
    }
    static void mysql_release(MYSQL *mysql)// 释放MySQL连接
    {
        if (mysql == NULL)
        {
            return;
        }
        mysql_close(mysql);
        return;
    }
    static bool mysql_exec(MYSQL *mysql, const std::string &sql)// 执行SQL语句
    {
        if (mysql_query(mysql, sql.c_str()) != 0)
        {
            const unsigned int errn = mysql_errno(mysql);
            const char *const sqlst = mysql_sqlstate(mysql);
            const char *const merr = mysql_error(mysql);
            ERR_LOG("mysql_query failed kind=%s errno=%u sqlstate=%s msg=%s", mysql_sql_kind_token(sql), errn,
                    sqlst != nullptr ? sqlst : "", merr != nullptr ? merr : "");
            return false;
        }
        return true;
    }
};

// Jsoncpp-API封装
class json_util
{
public:
    static bool serialize(const Json::Value &value, std::string &str)// Json对象序列化为字符串
    {
        Json::StreamWriterBuilder swb;
        std::unique_ptr<Json::StreamWriter> sw(swb.newStreamWriter());
        std::stringstream ss;
        int ret = sw->write(value, &ss);
        if (ret != 0)
        {
            ERR_LOG("json serialize failed!");
            return false;
        }
        str = ss.str();
        return true;
    }

    static bool unserialize(const std::string &str, Json::Value &value)// 字符串反序列化为Json对象
    {
        Json::CharReaderBuilder crb;
        std::unique_ptr<Json::CharReader> cr(crb.newCharReader());
        bool ret = cr->parse(str.c_str(), str.c_str() + str.size(), &value, nullptr);
        if (!ret)
        {
            ERR_LOG("json unserialize failed!");
            return false;
        }
        return true;
    }
};

// String-Split封装
class string_util
{
public:
    static int split(const std::string &in, const std::string &sep/*分隔符*/, std::vector<std::string> &arry)// 字符串分割函数
    {
        arry.clear();
        if (sep.empty())
        {
            arry.push_back(in);
            return 1;
        }

        size_t pos = 0;
        size_t idx = 0;
        while (idx < in.size())
        {
            pos = in.find(sep, idx);
            if (pos == std::string::npos)
            {
                arry.push_back(in.substr(idx));
                break;
            }
            if (pos != idx)
            {
                arry.push_back(in.substr(idx, pos - idx));
            }
            idx = pos + sep.size();
        }
        return (int)arry.size();
    }
};

// File-read封装
class file_util
{
public:
    static bool read(const std::string &filename, std::string &body)// 读取文件内容到字符串
    {
        std::ifstream file;
        file.open(filename.c_str(), std::ios::in | std::ios::binary);// 以二进制方式打开文件
        if (!file)
        {
            ERR_LOG("file open failed: %s", filename.c_str());
            return false;
        }

        // 获取文件大小
        file.seekg(0, std::ios::end);// 将文件指针移动到文件末尾
        std::streamsize size = file.tellg();// 获取文件指针当前位置，即文件大小
        if (size < 0)
        {
            ERR_LOG("file tellg failed: %s", filename.c_str());
            return false;
        }

        file.seekg(0, std::ios::beg);// 将文件指针移动回文件开头
        
        // 读取文件内容到字符串
        body.resize(static_cast<size_t>(size));
        if (size > 0)
        {
            file.read(&body[0], size);
        }

        // 检查文件读取是否成功
        if (file.good() == false)
        {
            ERR_LOG("file read failed: %s", filename.c_str());
            return false;
        }
        return true;
    }
};