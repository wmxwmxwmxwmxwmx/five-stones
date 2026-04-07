# five-stones：在线五子棋后端

`five-stones` 是一个 C++17 Linux 后端项目，整合 HTTP 与 WebSocket，覆盖注册登录、会话鉴权、大厅匹配、房间对战、观战同步和战绩落库。项目重点不只是“能跑”，还强调“可观测、可压测、可解释”。

## 项目速览

- **技术栈**：C++17、websocketpp/asio、MySQL、JsonCpp、可选 Redis
- **系统能力**：HTTP 用户接口 + WebSocket 实时对战三通道（`/hall`、`/room`、`/spectate`）
- **工程亮点**：
  - Redis 编译期开关 + 运行时降级（依赖异常时自动回退）
  - 登录链路稳健性增强（SQL 取数策略与失败日志完善）
  - k6 压测口径治理（setup 重试、default 不重试、失败分桶）
  - 新增缓存指标接口 `GET /metrics/cache`（本机只读）

## 关键设计与优化

### 1) 登录链路稳健性

- 调整登录 SQL 取数策略，规避异常数据下“非唯一结果”带来的不稳定行为。
- 补强登录失败日志，定位失败原因更直接（业务失败 vs 会话创建失败）。

### 2) 缓存可观测性

- 新增 `GET /metrics/cache`，输出：
  - `redis_hits_total`
  - `redis_miss_total`
  - `redis_errors_total`
  - `redis_hit_rate`
- 该接口仅允许 `127.0.0.1` / `::1`，用于本机压测联动与排障。

### 3) 压测可信度治理（k6）

- setup 阶段仅对瞬时网络错误做有限重试，避免脏前置数据。
- default 阶段不重试，保留真实失败率，避免“重试掩盖问题”。
- 登录脚本提供失败分桶指标，区分 `status=0`、`400`、`5xx`。

## 快速开始（5 分钟可跑通）

### 1) 安装依赖

```bash
sudo apt update
sudo apt install -y build-essential cmake libmysqlclient-dev libjsoncpp-dev libboost-system-dev
```

Redis 后端依赖 `redis-plus-plus` 和 `hiredis`，仅在需要 Redis 路径时安装。

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

### 3) 编译与运行

```bash
cmake -S . -B build
cmake --build build -j
./build/gobang_server 8080
```

访问：`http://127.0.0.1:8080/`

## Redis 开关与运行判定

### 编译期开关

```bash
# 关闭 Redis 后端
cmake -S . -B build_no_redis -DFIVE_STONES_WITH_REDIS=OFF

# 尝试启用 Redis 后端
cmake -S . -B build_redis_on -DFIVE_STONES_WITH_REDIS=ON
```

说明：`ON` 仅表示尝试启用；若依赖探测失败，CMake 会自动退回非 Redis 路径。

### 运行期判定

启动日志出现以下任一信息：

- `redis cache enabled: ...`：Redis 已启用
- `redis init failed, fallback to in-memory/mysql path: ...`：Redis 初始化失败并已降级

## 接口与状态码（核心）

- `POST /reg`
  - `200` 注册成功
  - `400` 参数错误 / 用户名占用
- `POST /login`
  - `200` 登录成功并返回 `Set-Cookie: SSID=...`
  - `400` 参数错误 / 用户名或密码错误
  - `500` 会话创建失败
- `GET /info`
  - `200` 返回当前用户信息
  - `400` 用户信息缺失（需重新登录）
- `GET /metrics/cache`（只读）
  - `200` 返回缓存命中/未命中/错误及命中率
  - `403` 非本机访问

## 性能与稳定性（可复现结论）

已使用 k6 在同机同参数下做分层压测（读路径与写路径分离）：

- `80 VUs / 2m`：读写路径均稳定，失败率为 0，延迟低
- `120 VUs / 2m`：仍稳定，登录写路径 `p95` 在阈值内
- `150 VUs / 2m`：登录写路径开始触达延迟拐点（`p95` 接近/超过 200ms）

工程解读：当前系统不是“错误率先崩”，而是“高并发下延迟先上升”，适合先做容量优化和并发治理。

详细压测方法见 [`scripts/k6/README.md`](scripts/k6/README.md)。

## 测试与验证

### 单元测试（gtest）

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### k6 压测

```bash
export BASE_URL=http://127.0.0.1:8080
./scripts/k6/run_k6_with_cleanup.sh run scripts/k6/info_user_cache.js
./scripts/k6/run_k6_with_cleanup.sh run scripts/k6/login_session_write.js
```

## 后续优化方向

- 在明确线程安全边界后，评估“单 Reactor + 多线程 run()”提升并发能力。
- 加入系统层观测（CPU、fd、队列、慢 SQL）做瓶颈归因闭环。
- 补充线上化能力：配置分环境、告警阈值、压力回归流水线。

## 常见问题

- `CMakeCache.txt` 显示 Redis ON，为何没生效？  
  依赖探测失败时会自动降级，最终以编译命令和启动日志为准。

- 如何确认宏注入？  
  检查 `build*/compile_commands.json` 是否出现 `-DFIVE_STONES_WITH_REDIS=1`。

## 目录结构

- `CMakeLists.txt`：顶层构建入口
- `five-stones/CMakeLists.txt`：服务目标与宏注入
- `five-stones/include/`：核心模块（`server.hpp`、`db.hpp`、`session.hpp` 等）
- `five-stones/src/main.cc`：程序入口
- `tests/`：单元测试
- `scripts/k6/`：压测脚本与数据清理脚本
