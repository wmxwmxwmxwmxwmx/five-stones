## five-stones：在线五子棋对战（HTTP + WebSocket）

### 项目简介
基于 **Linux/C++** 开发的实时对战后端，提供**登录鉴权**、**大厅匹配**、**房间对局**、**观战同步**与**战绩落库**等功能，完成 **HTTP + WebSocket** 一体化通信闭环。

核心功能：
- **HTTP**：静态资源（源码树内 `five-stones/resources/wwwroot/`，CMake 通过宏 `WWWROOT` 注入绝对路径）、注册/登录/用户信息接口
- **WebSocket**：大厅 `/hall` 匹配与房间列表，房间 `/room` 对局消息，观战 `/spectate` 快照与同步
- **数据落库**：MySQL 用户与战绩（积分、总局数、胜局数）

### 技术栈
- **语言/平台**：C++17、Linux
- **网络与并发**：WebSocket++（ASIO）、STL 线程与同步原语
- **数据与序列化**：MySQL C API（`libmysqlclient`）、JsonCpp
- **构建**：CMake 3.16+，产物为 `gobang_server`。顶层 [`CMakeLists.txt`](CMakeLists.txt) 含 `project()`，**必须在仓库根目录**执行 `cmake -S .`（子目录 [`five-stones/CMakeLists.txt`](five-stones/CMakeLists.txt) 不能单独作为工程根）
- **测试**：GoogleTest（`BUILD_TESTING` 默认开启；系统有 GTest 则优先用系统包，否则 FetchContent，见下文）

### 技术亮点
- **事件驱动 + URI 路由**：WebSocket++ 事件循环统一处理 HTTP 与 WebSocket，按 `/hall`、`/room`、`/spectate` 分发（见 [`server.hpp`](five-stones/include/server.hpp)）
- **分段匹配队列 + 多线程匹配**：按分数分池，条件变量阻塞等待，满两人配对建房（见 [`match.hpp`](five-stones/include/match.hpp)）
- **房间状态与广播**：落子、胜负、聊天、超时、再来一局，向双方与观战者广播（见 [`room.hpp`](five-stones/include/room.hpp)）
- **Cookie/SSID 会话**：登录下发 `SSID`，后续 HTTP/WS 从 Cookie 还原；定时器回收超时会话（见 [`session.hpp`](five-stones/include/session.hpp)）
- **在线映射与断线清理**：uid→连接映射，断连时清理并触发房间逻辑（见 [`online.hpp`](five-stones/include/online.hpp)、[`server.hpp`](five-stones/include/server.hpp)）

> 支持 **SSID 未过期免重复登录** 与 **观战快照恢复**；**不支持**断线回到原房间续弈（断开 `/room` 会走退出逻辑）。

---

## 快速开始

**怎么读本文**
- **先把服务跑起来**：按顺序看 **1 依赖 → 2 数据库 → 3 编译 → 4 启动与访问**。
- **改端口、数据库、静态目录**：看 **「环境变量（服务端）」** 与 [`app_config.hpp`](five-stones/include/app_config.hpp)。
- **跑测试 / MySQL 压测**：看 **「单元测试」** 与 **「MySQL 并发压测」**。

### 1) 安装依赖

需要：`mysqlclient`、`jsoncpp`、`boost_system`、`pthread`（CMake 已配置链接）。

Ubuntu 示例：

```bash
sudo apt update
sudo apt install -y build-essential cmake libmysqlclient-dev libjsoncpp-dev libboost-system-dev
```

### 2) 准备数据库

依赖 `user` 表，字段顺序与 [`db.hpp`](five-stones/include/db.hpp) 中 SQL 一致。`username` / `password` 等列长度需与代码里拼接的语句匹配（若改表结构请同步改 `db.hpp`）。

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

在**仓库根目录**（含顶层 `CMakeLists.txt`）执行：

```bash
cmake -S . -B build
cmake --build build -j
```

若需显式使用 Make：

```bash
cmake -S . -B build -G "Unix Makefiles"
cmake --build build -j
```

可执行文件：**`build/gobang_server`**（由 [`five-stones/CMakeLists.txt`](five-stones/CMakeLists.txt) 指定输出目录）。

### 4) 启动与访问

```bash
./build/gobang_server 8080
```

也可不设命令行参数，改用环境变量 **`SERVER_PORT`**（见下表）；**命令行端口优先于 `SERVER_PORT`**。

浏览器打开：**`http://127.0.0.1:<端口>/`**（默认静态入口为 `login.html`）。

**数据库与静态目录**：默认值与下表一致；启动前设置 `MYSQL_*`、`WWWROOT` 等即可覆盖，无需改代码。逻辑见 [`app_config.hpp`](five-stones/include/app_config.hpp)。

> **静态资源路径**：CMake 会把源码树内 `five-stones/resources/wwwroot/` 的绝对路径写入编译宏 `WWWROOT`；若未设置环境变量 `WWWROOT`，运行时一般**不依赖**当前工作目录是否在 `build/`。

### 5) 环境变量（服务端）

与 MySQL 压测、[`load_cfg`](tests/mysql_concurrency_stress_test.cc) 使用同一套 `MYSQL_*` 命名。

| 变量 | 含义 | 未设置时 |
|------|------|----------|
| `MYSQL_HOST` | MySQL 主机 | `127.0.0.1` |
| `MYSQL_USER` | 用户名 | `wmx` |
| `MYSQL_PASSWORD` | 密码 | `123456` |
| `MYSQL_DATABASE` | 库名 | `online_gobang` |
| `MYSQL_PORT` | MySQL 端口（须为合法整数） | `3306` |
| `SERVER_PORT` | HTTP/WebSocket 监听端口 | `8080` |
| `WWWROOT` | 静态资源根目录（覆盖编译期 `WWWROOT`） | 编译期注入路径 |
| `LOG_LEVEL` | 日志过滤级别 | 见下节 |

**监听端口**：`./gobang_server <port>` **>** `SERVER_PORT` **>** 默认 `8080`。`MYSQL_PORT` 只影响数据库，不是 Web 端口。

**生产建议**：密码等敏感配置用环境或密钥注入，勿提交仓库；可设 `LOG_LEVEL=error`。

### 6) 日志（`LOG_LEVEL`）

变量名见上表。实现见 [`logger.hpp`](five-stones/include/logger.hpp)。

- 取值（不区分大小写）：`debug`（默认，无法解析时同 `debug`）、`info`、`error`。只输出「严重度不低于当前阈值」的日志：`debug` 输出全部三级，`info` 输出 INFO+ERROR，`error` 仅 ERROR。
- **生产**：建议 `LOG_LEVEL=error`，减少噪音。
- **流**：ERROR → **stderr**；DEBUG / INFO → **stdout**（便于容器分流）。

### 7) 单元测试（GoogleTest + CTest）

源码在 [`tests/`](tests/)，目标 **`five_stones_tests`**。用例大致三类：

- **并发 / 压力**：[`stress_concurrency_test.cc`](tests/stress_concurrency_test.cc)（`match_queue`、`session_manager` 等）
- **边界 / 异常**：[`boundary_exception_test.cc`](tests/boundary_exception_test.cc)（`json_util`、`string_util`、`match_queue`、`session` 等）
- **MySQL 压测**：[`mysql_concurrency_stress_test.cc`](tests/mysql_concurrency_stress_test.cc)（无 MySQL 或连不上时相关用例 **SKIP**，见下节）

另：高负载单线程用例 `Stress.DISABLED_MatchQueuePushPopSingleThreaded` 默认禁用，本地可去掉 `DISABLED_` 运行。不含 HTTP 端到端自动化。

**必须在仓库根目录配置**，勿在 `tests/` 下单独当 CMake 根执行 `cmake ..`。

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

也可直接运行 `build/tests/five_stones_tests`。编译时通过 `-include tests/wsserver_for_tests.h` 注入 `wsserver_t`，以编译 [`session.hpp`](five-stones/include/session.hpp)。

**测试构建选项**

- 关闭测试：`cmake ... -DBUILD_TESTING=OFF`
- 拉取 GoogleTest：若未找到系统 GTest，CMake 会 FetchContent（默认 GitHub，需网络）。镜像示例：  
  `cmake -S . -B build -DFIVE_STONES_GOOGLETEST_REPOSITORY=https://gitee.com/mirrors/googletest.git`
- 使用系统 GTest：安装发行版提供的开发包并能被 `find_package` 找到即可（包名因发行版而异，如 `libgtest-dev`）

### 8) MySQL 并发压测

默认**可不设环境变量**：连接默认与 [`app_config.hpp`](five-stones/include/app_config.hpp) 及上表一致。MySQL 可达且存在 `user` 表时，4 条用例执行；否则 **SKIP**，整体仍可通过。

可选覆盖连接与规模：

```bash
export MYSQL_HOST=127.0.0.1
export MYSQL_USER=你的用户
export MYSQL_PASSWORD=你的密码
export MYSQL_DATABASE=online_gobang_test
# 可选：MYSQL_PORT
# STRESS_THREADS：默认 128，范围 1～512
# STRESS_ITERS：默认 500，范围 1～100000
ctest --test-dir build --output-on-failure
```

**数据清理**：用例会删除 **`username LIKE 'ft%'`** 的行（压测专用前缀）。请在**独立测试库**上跑压测；若与业务库共用库名，仍有误删风险，需谨慎。

**说明**：压测里若 `MYSQL_PORT` 无法解析会**忽略**该变量；**服务端 `gobang_server`** 在 [`load_server_cfg`](five-stones/include/app_config.hpp) 中对非法 `MYSQL_PORT` 会**报错退出**，行为不同。

`user_table` 为单连接并对部分 API 加锁；压测用于观察多线程下稳定性与吞吐。

### 9) 可选：VS Code / Cursor（CMake Tools）

工作区含 [`.vscode/settings.json`](.vscode/settings.json)：`cmake.sourceDirectory` 指向仓库根，Linux 上推荐 **Unix Makefiles** 与 `CMAKE_MAKE_PROGRAM=/usr/bin/make`，避免子目录误配或 Windows 路径残留。若仍失败，检查用户/远程设置中的 `cmake.generator` 等是否含无效路径。

生成 `compile_commands.json`（如给 clangd）：配置时加 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`。

---

## 目录结构
- [`.gitignore`](.gitignore)：忽略 `build/` 等
- [`CMakeLists.txt`](CMakeLists.txt)：顶层工程、`add_subdirectory(five-stones)`、测试时 `add_subdirectory(tests)`
- [`tests/`](tests/)：GoogleTest（[`tests/CMakeLists.txt`](tests/CMakeLists.txt)、各 `.cc`、`wsserver_for_tests.h`）
- [`five-stones/include/match_queue.hpp`](five-stones/include/match_queue.hpp)：匹配队列模板（[`match.hpp`](five-stones/include/match.hpp) 包含）
- [`.vscode/settings.json`](.vscode/settings.json)（可选）：CMake Tools 约定
- [`five-stones/CMakeLists.txt`](five-stones/CMakeLists.txt)：`gobang_server`、`WWWROOT`、链接库
- [`five-stones/src/main.cc`](five-stones/src/main.cc)：入口，`load_server_cfg` 后构造服务并 `start`
- [`five-stones/include/app_config.hpp`](five-stones/include/app_config.hpp)：运行时环境变量与默认配置
- [`five-stones/include/`](five-stones/include/)：`server.hpp`、`db.hpp`、`session.hpp`、`online.hpp`、`match.hpp`、`room.hpp` 等
- [`five-stones/resources/wwwroot/`](five-stones/resources/wwwroot/)：前端静态页

---

## 核心链路

### HTTP：登录与会话
1. 浏览器 `POST /login`
2. `server.hpp::login` → `user_table::login`
3. `session_manager::create_session` 生成 `SSID`
4. `Set-Cookie: SSID=...`；后续请求用 Cookie 还原 uid

### WS：大厅匹配（/hall）
1. `WS /hall` → `wsopen_game_hall`（`hall_ready`）
2. `{optype:match_start}` → `wsmsg_game_hall` → `matcher::add(uid)`
3. 匹配线程配对 → `room_manager::create_room` → `match_success`

### WS：房间对局（/room）
1. `WS /room` → `wsopen_game_room`（`room_ready`）
2. 落子/聊天等 → `wsmsg_game_room` → `room::handle_request`
3. `room::broadcast` 同步双方与观战者

### WS：观战（/spectate）
1. `game_room.html?spectate=1&room_id=...`
2. `WS /spectate` 后发 `spectate_join`
3. `wsmsg_spectate`：加入观战表，回 `spectate_ready` 与棋盘快照
4. 房间内广播同样发给观战者

---

## 注意事项
- `/room` 断连会触发退出与房间清理，**无**断线续局；SSID 未过期可免重复登录（与上文「项目简介」一致）。
