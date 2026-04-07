# five-stones：在线五子棋对战后端

`five-stones` 是基于 C++17 的 Linux 后端服务，提供 HTTP 与 WebSocket 一体化能力，包含注册登录、会话鉴权、大厅匹配、房间对局、观战同步与战绩落库。

## 功能概览

- HTTP：静态资源、`POST /reg`、`POST /login`、`GET /info`
- WebSocket：`/hall`、`/room`、`/spectate` 三类实时通道
- 数据存储：MySQL 用户与战绩
- 可选缓存：Redis（编译期可开关，运行期可降级）

## 快速开始

### 1) 安装依赖

Ubuntu 示例：

```bash
sudo apt update
sudo apt install -y build-essential cmake libmysqlclient-dev libjsoncpp-dev libboost-system-dev
```

说明：Redis 后端依赖 `redis-plus-plus` 与 `hiredis`，仅在你需要 Redis 缓存路径时安装。

### 2) 准备数据库

```sql
CREATE DATABASE IF NOT EXISTS online_gobang DEFAULT CHARACTER SET utf8mb4;
USE online_gobang;

CREATE TABLE IF NOT EXISTS user (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  username VARCHAR(64) NOT NULL UNIQUE,
  password VARCHAR(128) NOT NULL,
  score BIGINT UNSIGNED NOT NULL DEFAULT 1000,
  total_count INT NOT NULL DEFAULT 0,
  win_count INT NOT NULL DEFAULT 0,
  PRIMARY KEY (id)
);
```

### 3) 编译

必须在仓库根目录（含顶层 `CMakeLists.txt`）执行：

```bash
cmake -S . -B build
cmake --build build -j
```

可执行文件输出到 `build/gobang_server`。

### 4) 启动

```bash
./build/gobang_server 8080
```

浏览器访问 `http://127.0.0.1:8080/`。

## Redis 开关与判定（重点）

### 编译期是否启用 Redis 代码

- 默认尝试启用：`-DFIVE_STONES_WITH_REDIS=ON`
- 显式关闭：`-DFIVE_STONES_WITH_REDIS=OFF`
- 当配置为 ON 但未找到 `redis++/hiredis` 时，CMake 会警告并关闭 Redis 后端

建议：

```bash
# 无 Redis 后端
cmake -S . -B build_no_redis -DFIVE_STONES_WITH_REDIS=OFF

# 尝试启用 Redis 后端
cmake -S . -B build_redis_on -DFIVE_STONES_WITH_REDIS=ON
```

### 运行期是否真正使用 Redis

若二进制已编进 Redis 分支，服务启动时会出现以下日志之一：

- `redis cache enabled: ...`：已连接 Redis，正在使用缓存
- `redis init failed, fallback to in-memory/mysql path: ...`：初始化失败，已降级到内存/MySQL 路径

若这两条都没有，通常说明当前二进制未编入 Redis 分支（可检查 `compile_commands.json` 是否含 `-DFIVE_STONES_WITH_REDIS=1`）。

## HTTP 接口与状态码

### `POST /reg`

- `200`：注册成功
- `400`：请求体 JSON 非法、缺少用户名/密码、用户名已占用

### `POST /login`

- `200`：登录成功（返回 `Set-Cookie: SSID=...`）
- `400`：请求体 JSON 非法、缺少用户名/密码、用户名或密码错误
- `500`：会话创建失败

### `GET /info`

- `200`：返回当前用户信息
- `400`：找不到用户信息（需重新登录）

## 服务端环境变量

| 变量 | 含义 | 默认值 |
|------|------|--------|
| `MYSQL_HOST` | MySQL 主机 | `127.0.0.1` |
| `MYSQL_USER` | MySQL 用户名 | `wmx` |
| `MYSQL_PASSWORD` | MySQL 密码 | `123456` |
| `MYSQL_DATABASE` | MySQL 库名 | `online_gobang` |
| `MYSQL_PORT` | MySQL 端口 | `3306` |
| `REDIS_HOST` | Redis 主机 | `127.0.0.1` |
| `REDIS_PORT` | Redis 端口 | `6379` |
| `REDIS_PASSWORD` | Redis 密码 | 空 |
| `REDIS_DB` | Redis DB 索引 | `0` |
| `REDIS_TIMEOUT_MS` | Redis 超时毫秒 | `200` |
| `SERVER_PORT` | 服务监听端口 | `8080` |
| `WWWROOT` | 静态资源目录 | 编译期注入路径 |
| `LOG_LEVEL` | 日志级别（`debug/info/error`） | `debug` |

端口优先级：命令行参数 `./gobang_server <port>` > `SERVER_PORT` > 默认 `8080`。

## 测试与压测

### 单元测试（gtest）

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

测试目标为 `build/tests/five_stones_tests`。

### k6 压测（Redis 前后对比）

压测脚本位于 `scripts/k6/`，详见 [`scripts/k6/README.md`](scripts/k6/README.md)。

推荐对比方式：同一台机器、同一组 k6 参数下分别运行：

1. `FIVE_STONES_WITH_REDIS=OFF`（基线）
2. `FIVE_STONES_WITH_REDIS=ON` 且 Redis 可用（对照）

对比关注：`http_reqs/s`、`http_req_duration p(95)/p(99)`、`http_req_failed`。

## 常见问题排查

### 为什么有 `build`、`build_no_redis`、`build_redis_on`？

这些目录不是源码创建，而是 CMake 在执行 `cmake -B <目录名>` 时自动创建的构建目录。

### 如何确认 Redis 宏是否真的注入？

查看 `build/compile_commands.json` 中 `main.cc` 对应编译命令，是否包含 `-DFIVE_STONES_WITH_REDIS=1`。

### `CMakeCache.txt` 显示 ON，但实际没走 Redis，为什么？

`FIVE_STONES_WITH_REDIS:BOOL=ON` 表示选项倾向开启；若依赖探测失败，配置阶段仍可能关闭 Redis 分支，最终编译命令里不会出现 `-DFIVE_STONES_WITH_REDIS=1`。

## 目录结构

- `CMakeLists.txt`：顶层工程入口
- `five-stones/CMakeLists.txt`：服务目标、宏注入、链接配置
- `five-stones/include/`：核心头文件（`server.hpp`、`db.hpp`、`session.hpp` 等）
- `five-stones/src/main.cc`：程序入口
- `five-stones/resources/wwwroot/`：静态页面
- `tests/`：gtest 测试
- `scripts/k6/`：k6 压测脚本与清理脚本
