# k6 压测说明

这份文档用于回答三个问题：**测了什么、怎么测、结果怎么解释**。  
目标是让压测结果不仅“有数字”，还“有因果解释”。

## 测试设计（核心思路）

- **读写分离**：把用户信息读取与登录写入拆成两条脚本，避免互相污染。
- **口径一致**：同机、同参数、同数据规模，Redis A/B 只改一个变量。
- **可解释性**：除 k6 外部指标外，联动 `GET /metrics/cache` 观测缓存内部命中情况。

## 脚本与职责

| 脚本 | 路径定位 | 主要目标 | 口径说明 |
|------|----------|----------|----------|
| [`info_user_cache.js`](info_user_cache.js) | 读路径 | 压 `GET /info`，观察缓存收益 | `setup()` 预注册+预登录，`default(data)` 只压读请求 |
| [`login_session_write.js`](login_session_write.js) | 写路径 | 压 `POST /login`，观察会话创建与写压力 | `setup()` 预注册（有限重试），`default()` 单次请求不重试 |

## k6 生命周期口径

- `setup()`：只执行一次，做准备动作；允许有限重试，仅处理瞬时网络错误（如 EOF）。
- `default(data)`：每个 VU 循环执行主压测逻辑；不做业务掩盖性重试。

关键约束：

- `K6_LOGIN_USERS >= K6_VUS`，否则部分 VU 会因无账号可用而空转，导致结果失真。

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `BASE_URL` | `http://localhost:8080` | 服务地址（脚本会清理末尾空白和标点） |
| `K6_PASSWORD` | `k6test123` | 压测账号密码 |
| `K6_VUS` | `20` | 并发 VU |
| `K6_DURATION` | `1m` | 压测时长 |
| `K6_INFO_ITERS` | `50` | `info_user_cache.js` 每轮 `/info` 次数 |
| `K6_LOGIN_USERS` | `100` | `login_session_write.js` 预注册账号数（建议 `>= K6_VUS`） |
| `K6_LOGIN_ITERS` | `20` | `login_session_write.js` 每轮登录次数 |

## 执行模板（可直接复制）

### 低压基线（20 VUs）

```bash
export BASE_URL=http://127.0.0.1:8080
K6_VUS=20 K6_DURATION=1m K6_INFO_ITERS=50 k6 run scripts/k6/info_user_cache.js
K6_VUS=20 K6_DURATION=1m K6_LOGIN_USERS=20 K6_LOGIN_ITERS=20 k6 run scripts/k6/login_session_write.js
```

### 中压回归（80 / 120 VUs）

```bash
export BASE_URL=http://127.0.0.1:8080
K6_VUS=80  K6_DURATION=2m K6_INFO_ITERS=80 k6 run scripts/k6/info_user_cache.js
K6_VUS=80  K6_DURATION=2m K6_LOGIN_USERS=80  K6_LOGIN_ITERS=40 k6 run scripts/k6/login_session_write.js

K6_VUS=120 K6_DURATION=2m K6_INFO_ITERS=80 k6 run scripts/k6/info_user_cache.js
K6_VUS=120 K6_DURATION=2m K6_LOGIN_USERS=120 K6_LOGIN_ITERS=40 k6 run scripts/k6/login_session_write.js
```

### 高压拐点（150 VUs）

```bash
export BASE_URL=http://127.0.0.1:8080
K6_VUS=150 K6_DURATION=2m K6_INFO_ITERS=80 k6 run scripts/k6/info_user_cache.js
K6_VUS=150 K6_DURATION=2m K6_LOGIN_USERS=150 K6_LOGIN_ITERS=40 k6 run scripts/k6/login_session_write.js
```

## Redis A/B 对比方法

1. 编译 A 组（无 Redis）：`-DFIVE_STONES_WITH_REDIS=OFF`
2. 编译 B 组（有 Redis）：`-DFIVE_STONES_WITH_REDIS=ON` 且 Redis 可连
3. 两组分别执行同一套 k6 命令
4. 记录并对比：
   - `http_req_failed`
   - `http_req_duration p(95)/p(99)`
   - `http_reqs`
5. 每组压测前后调用一次 `GET /metrics/cache`，观察 `hit/miss/errors/hit_rate` 变化

## 结果解读建议

按这个顺序解读结果：

1. **先看失败率**：是否有稳定性风险（`http_req_failed`）
2. **再看延迟**：是否触碰 SLO（`p95/p99`）
3. **最后看吞吐**：容量上限和效率（`http_reqs`）
4. **结合缓存指标**：Redis 是否真正生效，而非“看起来变快”

## 常见误区

- `setup` 有瞬时失败，不等于主路径失败。
- 在 `default` 做重试会掩盖真实失败率，不适合作为容量结论口径。
- `K6_LOGIN_USERS < K6_VUS` 会导致 VU 空转，吞吐和延迟数据失真。
- `GET /metrics/cache` 返回 `403` 通常是本机访问限制命中（接口仅允许本机）。

## 自动清理与注意事项

推荐用包装脚本自动清理压测用户：

```bash
export BASE_URL=http://127.0.0.1:8080
./scripts/k6/run_k6_with_cleanup.sh run scripts/k6/info_user_cache.js
./scripts/k6/run_k6_with_cleanup.sh run scripts/k6/login_session_write.js
```

补充说明：

- 清理脚本删除 `user` 表中匹配 `^k6_(vu|login)_` 的账号
- 依赖本机 `mysql` 客户端
- 若进程被 `SIGKILL` 强杀，自动清理不会触发，需要手动补跑 `cleanup_k6_mysql.sh`