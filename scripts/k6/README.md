# k6 压测说明（QPS / 并发 / Redis 前后对比）

本目录用于压测 `gobang_server` 的 HTTP 路径，重点观察 Redis 开关前后的吞吐与延迟差异。

前置条件：

- 服务端已启动（见根目录 [`README.md`](../../README.md)）
- MySQL `user` 表可用
- 本机已安装 k6（[官方安装文档](https://grafana.com/docs/k6/latest/set-up/install-k6/)）

## 脚本与测试目标

| 脚本 | 主要目标 | 说明 |
|------|----------|------|
| [`info_user_cache.js`](info_user_cache.js) | 用户信息读路径 | `setup()` 一次性为每个 VU 预注册并登录，`default(data)` 仅循环 `GET /info`；用户名会附带运行标识避免历史数据冲突，setup 的 POST 请求含有限重试以缓解瞬时 EOF |
| [`login_session_write.js`](login_session_write.js) | 登录写路径 | `setup()` 预注册用户（含有限重试），`default()` 循环 `POST /login`，重点观察会话创建与 Redis `setSession` 写入开销 |

## k6 生命周期说明（与当前脚本一致）

- `export function setup()`：测试开始前仅执行一次，适合准备全局数据  
  `login_session_write.js` 在这里预注册 `k6_login_*`；`info_user_cache.js` 在这里预注册并登录 `k6_vu_*`，返回每个 VU 的 cookie
- `export default function (data)`：每个 VU 在压测期间循环执行，承担主要请求压力

当前 `login_session_write.js` 的 `setup()` 不返回数据；`info_user_cache.js` 的 `setup()` 返回 cookie 列表供 `default(data)` 使用。
两脚本 setup 中的 `POST /reg`、`POST /login` 已加入有限重试（仅针对网络层瞬时失败如 `status=0/EOF`），不改变业务 400 的判定口径。

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `BASE_URL` | `http://localhost:8080` | 目标服务地址（脚本会自动去除末尾空白和中英文标点） |
| `K6_PASSWORD` | `k6test123` | 压测账号密码 |
| `K6_VUS` | `20` | 并发 VU 数 |
| `K6_DURATION` | `1m` | 压测时长 |
| `K6_INFO_ITERS` | `50` | `info_user_cache.js` 每次 default 中 `/info` 次数 |
| `K6_LOGIN_USERS` | `100` | `login_session_write.js` 预注册用户数，建议不小于 `K6_VUS` |
| `K6_LOGIN_ITERS` | `20` | `login_session_write.js` 每次 default 中登录次数 |

## 运行方式

推荐使用包装脚本（自动清理 MySQL 压测数据）：

```bash
export BASE_URL=http://127.0.0.1:8080
./scripts/k6/run_k6_with_cleanup.sh run scripts/k6/info_user_cache.js
./scripts/k6/run_k6_with_cleanup.sh run scripts/k6/login_session_write.js
```

手动方式：

```bash
k6 run scripts/k6/info_user_cache.js
./scripts/k6/cleanup_k6_mysql.sh
```

## Redis 前后对比（A/B）

目标：比较“关闭 Redis 后端”和“开启 Redis 且可连”两种服务端形态。

1. 基线 A：`-DFIVE_STONES_WITH_REDIS=OFF`，编译并启动服务，执行同一条 k6 命令
2. 基线 B：`-DFIVE_STONES_WITH_REDIS=ON`，Redis 服务可用，执行同一条 k6 命令
3. 两组都记录：全局 `http_reqs`、`http_req_duration p(95)/p(99)`、`http_req_failed`，并重点对比 `name=GET /info` 的专属指标

对比原则：同机器、同数据量、同 `K6_VUS/K6_DURATION/K6_INFO_ITERS`，仅改变 Redis 条件。

## 数据清理与注意事项

`cleanup_k6_mysql.sh` 会删除用户名匹配 `^k6_(vu|login)_` 的行。

可用变量：

- `MYSQL_HOST` / `MYSQL_USER` / `MYSQL_PASSWORD` / `MYSQL_DATABASE` / `MYSQL_PORT`
- `K6_SKIP_MYSQL_CLEANUP=1`（跳过自动清理）

注意：

- 清理脚本依赖本机 `mysql` 客户端
- 若进程被强杀（如 `SIGKILL`），自动清理不会触发，需要手动补跑
- 本清理仅处理 MySQL，不会强制删除 Redis 键