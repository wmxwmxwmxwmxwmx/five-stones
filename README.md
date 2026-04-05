## five-stones：在线五子棋对战（HTTP + WebSocket）

### 项目简介
基于 **Linux/C++** 开发的实时对战后端，提供**登录鉴权**、**大厅匹配**、**房间对局**、**观战同步**与**战绩落库**等功能，完成 **HTTP + WebSocket** 一体化通信闭环。

核心功能：
- **HTTP**：静态资源服务（源码树内 `five-stones/resources/wwwroot/`，CMake 通过宏 `WWWROOT` 指向绝对路径），注册/登录/用户信息接口
- **WebSocket**：大厅（`/hall`）匹配与房间列表，房间（`/room`）对局消息，观战（`/spectate`）快照与同步
- **数据落库**：MySQL 用户与战绩（积分、总局数、胜局数）

### 技术栈
- **语言/平台**：C++17、Linux
- **网络与并发**：WebSocket++（ASIO）、STL 线程库（`thread/mutex/condition_variable`）
- **数据与序列化**：MySQL C API（`libmysqlclient`）、JsonCpp
- **构建**：CMake 3.16+，生成单目标可执行文件 `gobang_server`；顶层 [`CMakeLists.txt`](CMakeLists.txt) 含 `project()`，**必须在仓库根目录**作为 `-S` 源码树配置（子目录 [`five-stones/CMakeLists.txt`](five-stones/CMakeLists.txt) 仅 `add_executable`，不能单独当作工程根）
- **测试**：GoogleTest（`BUILD_TESTING` 默认开启；若系统已安装可被 `find_package` 找到的 GTest，则不再下载；否则通过 FetchContent 拉取，见下文）

### 技术亮点
- **事件驱动 + URI 路由分发**：基于 WebSocket++ 的事件循环统一接入 HTTP 与 WebSocket，按 URI（`/hall`、`/room`、`/spectate`）分发到不同业务处理函数（见 `five-stones/include/server.hpp`）。
- **分段匹配队列 + 多线程匹配**：按分数分池（普通/高手/大神）进入不同匹配队列，匹配线程用条件变量阻塞等待（队列人数≥2）并完成配对建房（见 `five-stones/include/match.hpp`）。
- **房间状态管理与广播同步**：封装落子校验、胜负判定、聊天、超时、再来一局，以及向双方玩家 + 观战者广播消息（见 `five-stones/include/room.hpp`）。
- **Cookie/SSID 会话鉴权 + 定时回收**：登录下发 `SSID` 写入 Cookie，后续 HTTP/WS 从 Cookie 还原会话；通过定时器实现会话超时删除（在线期间可切换为永久、断连后恢复超时回收，见 `five-stones/include/session.hpp`）。
- **在线连接映射与断线清理**：维护 uid→连接 的大厅/房间映射，断开连接时清理映射并触发房间退出逻辑（见 `five-stones/include/online.hpp`、`five-stones/include/server.hpp`）。

> 说明：当前实现支持 **身份续用（SSID 未过期时免重复登录）** 与 **观战快照恢复**；不支持“玩家断线后回到原对局房间继续下棋”的断线重连续局能力（断开 `/room` 会触发房间退出处理）。

---

## 快速开始

### 1) 安装依赖
链接库与原先一致：`mysqlclient`、`jsoncpp`、`boost_system`、`pthread`（CMake 中通过 `Threads::Threads` 等配置）。

Ubuntu 示例：

```bash
sudo apt update
sudo apt install -y build-essential cmake libmysqlclient-dev libjsoncpp-dev libboost-system-dev
```

（`build-essential` 提供 `g++` 与 `make`，与上文 Unix Makefiles 构建方式一致。）

### 2) 准备数据库
代码依赖一个 `user` 表，字段顺序与查询语句在 `five-stones/include/db.hpp` 中固定（`id, username, password, score, total_count, win_count`）。

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

### 3) 编译与运行

在**仓库根目录**（包含顶层 `CMakeLists.txt` 的目录）执行 out-of-source 构建：

```bash
cmake -S . -B build
cmake --build build -j
```

若本机未安装 Ninja，或希望显式使用 Make，可指定生成器（需已安装 `make`，Ubuntu 上通常随 `build-essential` 提供）：

```bash
cmake -S . -B build -G "Unix Makefiles"
cmake --build build -j
```

可执行文件默认输出到 **`build/gobang_server`**（由子目录 `five-stones/CMakeLists.txt` 中 `RUNTIME_OUTPUT_DIRECTORY` 指定）。

#### 日志（`LOG_LEVEL`）

日志宏见 [`five-stones/include/logger.hpp`](five-stones/include/logger.hpp)：

- 环境变量 **`LOG_LEVEL`**：`debug`（默认，未设置或无法解析时同 `debug`）/`info`/`error`，不区分大小写。仅当消息严重度不低于设定阈值时输出（`debug` 输出 DEBUG/INFO/ERROR，`info` 输出 INFO/ERROR，`error` 仅输出 ERROR）。
- **生产建议**：`LOG_LEVEL=error`，减少噪音与错误路径以外的日志量。
- **输出流**：**ERROR** 写入 **stderr**，DEBUG / INFO 写入 **stdout**，便于容器或采集侧按流分流。

#### 单元测试（GoogleTest + CTest）

默认会构建测试目标 `five_stones_tests`（源码在 [`tests/`](tests/)）。**当前用例覆盖三类**：**并发/压力**（[`stress_concurrency_test.cc`](tests/stress_concurrency_test.cc)：`match_queue` 多生产者、`wait` 唤醒、`session_manager` 并发创建/并发读）、**异常/边界**（[`boundary_exception_test.cc`](tests/boundary_exception_test.cc)：`json_util`/`string_util` 非法或边界输入、`match_queue` 空 `pop`、`session` 非法 ssid 等），以及 **MySQL 并发压测**（[`mysql_concurrency_stress_test.cc`](tests/mysql_concurrency_stress_test.cc)：默认连接与 `main.cc` 相同，本机无 MySQL 或连不上时 4 条用例 **SKIP**，见下文）。不含 HTTP/端到端脚本；高负载单线程用例 `Stress.DISABLED_MatchQueuePushPopSingleThreaded` 默认禁用，本地可去掉 `DISABLED_` 运行。

**必须在仓库根目录**配置（含 `project()` 的顶层 [`CMakeLists.txt`](CMakeLists.txt)），勿在 `tests/` 下单独执行 `cmake ..`。

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

也可直接运行 `build/tests/five_stones_tests`（路径随生成器可能略有不同）。测试目标通过 `-include tests/wsserver_for_tests.h` 注入 `wsserver_t` 定义，以便编译 [`session.hpp`](five-stones/include/session.hpp)。

#### MySQL 并发压测

默认**无需设置环境变量**：连接参数与 [`five-stones/src/main.cc`](five-stones/src/main.cc) 一致（`127.0.0.1`、`wmx`、`123456`、`online_gobang`、`3306`）。直接运行 `ctest` 或 `build/tests/five_stones_tests` 时，若 MySQL 可达且存在 `user` 表，则 4 条用例会**真正执行**；若连不上则 **SKIPPED**，整体仍算通过。

可选：用环境变量覆盖连接或压测规模（无需 `RUN_MYSQL_STRESS`）：

```bash
export MYSQL_HOST=127.0.0.1
export MYSQL_USER=你的用户
export MYSQL_PASSWORD=你的密码
export MYSQL_DATABASE=online_gobang_test
# 可选：MYSQL_PORT、STRESS_THREADS（默认 16，上限 512）、STRESS_ITERS（默认 100，上限 100000）
ctest --test-dir build --output-on-failure
```

**建议**在独立测试库上跑压测；若使用与线上一致的库名，用例仅删除 `username LIKE 'fstress%'` 的行，但仍需谨慎。`user_table` 为单连接且对部分 API 加锁，**insert 等路径未加锁**；压测用于观察多线程下的稳定性与可调参吞吐。

- **关闭测试**（跳过 GTest 与 `tests/` 子目录，适合仅需发布的构建）：配置时加 `-DBUILD_TESTING=OFF`。
- **拉取 googletest**：若未通过 `find_package(GTest)` 找到系统包，CMake 会使用 FetchContent 从 Git 克隆（默认仓库为 GitHub，**需要能访问外网或代理**）。若无法访问 GitHub，可在首次配置时指定镜像，例如：

```bash
cmake -S . -B build -DFIVE_STONES_GOOGLETEST_REPOSITORY=https://gitee.com/mirrors/googletest.git
```

- **使用系统 GTest**（可选）：安装发行版提供的 GTest 开发包并能被 CMake 找到后，将优先使用系统库，无需克隆（具体包名因发行版而异，例如部分环境为 `libgtest-dev`）。

#### VS Code / Cursor（CMake Tools）

工作区已包含 [`.vscode/settings.json`](.vscode/settings.json)：将 `cmake.sourceDirectory` 指向仓库根，并在 Linux 上使用 **Unix Makefiles** 与 `CMAKE_MAKE_PROGRAM=/usr/bin/make`，避免误用子目录为 CMake 根目录、或全局设置里残留的 Windows/Qt Ninja 路径导致配置失败。若你在其他机器上仍遇到类似问题，请在**用户设置**（含 Remote-SSH 的远程设置）中检查 `cmake.cmakePath`、`cmake.generator`、`cmake.configureSettings` 是否含有 `D:\...` 等无效路径。

生成 `compile_commands.json` 供 clangd 等使用时，可在配置时加上 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`（CMake Tools 中也可勾选对应选项）。

```bash
./build/gobang_server 8080
```

然后访问：

- `http://127.0.0.1:8080/`（默认静态页为 `login.html`）

> 数据库连接参数在 `five-stones/src/main.cc` 中配置（默认 host=`127.0.0.1`、user=`wmx`、pass=`123456`、db=`online_gobang`、port=`3306`）。

> **WWWROOT**：CMake 构建时为编译宏 `WWWROOT` 注入**源码树内** `five-stones/resources/wwwroot/` 的绝对路径，运行时**不依赖**当前工作目录是否位于 `build/`。

---

## 目录结构
- [`.gitignore`](.gitignore)：忽略 `build/`、常见 CMake 缓存与本地编译产物等
- `CMakeLists.txt`：顶层工程，`project()`、`add_subdirectory(five-stones)`、`BUILD_TESTING` 时 `add_subdirectory(tests)`
- `tests/`：GoogleTest（[`tests/CMakeLists.txt`](tests/CMakeLists.txt)、[`stress_concurrency_test.cc`](tests/stress_concurrency_test.cc)、[`boundary_exception_test.cc`](tests/boundary_exception_test.cc)、[`mysql_concurrency_stress_test.cc`](tests/mysql_concurrency_stress_test.cc)、[`wsserver_for_tests.h`](tests/wsserver_for_tests.h)）
- `five-stones/include/match_queue.hpp`：匹配队列模板（由 [`match.hpp`](five-stones/include/match.hpp) 包含，供并发单测直接使用）
- `.vscode/settings.json`（可选提交）：CMake Tools 在本仓库下的源码目录与生成器约定
- `five-stones/CMakeLists.txt`：目标 `gobang_server`、包含路径、`WWWROOT` 宏、链接库
- `five-stones/src/main.cc`：程序入口，构造 `gobang_server` 并 `start(port)`
- `five-stones/include/`：头文件（`server.hpp`、`db.hpp` 等）
  - `server.hpp`：HTTP/WS 回调注册、URI 分发与业务编排
  - `db.hpp`：`user_table`，MySQL 用户与战绩读写
  - `session.hpp` / `online.hpp` / `match.hpp` / `room.hpp` 等
- `five-stones/resources/wwwroot/`：前端静态页面（登录/大厅/房间）

---

## 核心链路

### HTTP：登录与会话
1. 浏览器 `POST /login`
2. `server.hpp::login` -> `user_table::login`
3. `session_manager::create_session` 生成 `SSID`
4. `Set-Cookie: SSID=...` 下发；后续请求通过 Cookie 还原 uid

### WS：大厅匹配（/hall）
1. 前端 `WS /hall` 建连 -> `wsopen_game_hall`（进入大厅映射、返回 `hall_ready`）
2. 点击“开始匹配”发送 `{optype:match_start}` -> `wsmsg_game_hall` -> `matcher::add(uid)`
3. 匹配线程从队列取两人 -> `room_manager::create_room` -> 给双方推送 `match_success`

### WS：房间对局（/room）
1. 前端 `WS /room` 建连 -> `wsopen_game_room`（校验会话、进入房间映射、返回 `room_ready`）
2. 发送落子/聊天/超时/重开等消息 -> `wsmsg_game_room` -> `room::handle_request`
3. `room::broadcast` 同步给双方玩家（观战者也会收到同样广播）

### WS：观战同步（/spectate）
1. 大厅点击观战入口 -> `game_room.html?spectate=1&room_id=...`
2. 前端 `WS /spectate` 建连后发送 `spectate_join`
3. `wsmsg_spectate`：将连接加入房间观战表，回 `spectate_ready + board_snapshot`
4. 后续房间内 `broadcast` 的 `put_chess/timeout/rematch_start/...` 会同时发送给观战者，实现实时同步

---

## 注意事项
- 当前行为：玩家 `/room` 断开会触发退出处理并可能导致房间销毁，因此不支持断线重连续局（但 SSID 未过期时可免重复登录）。

