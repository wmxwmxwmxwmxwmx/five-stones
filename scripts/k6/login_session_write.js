// 压测 POST /login（会话创建 + Redis setSession 写入路径，启用 Redis 时）。
// setup() 预注册 K6_LOGIN_USERS 个用户，VU 仅循环登录，避免注册成为瓶颈。
//
// 依赖：BASE_URL；可选 K6_PASSWORD、K6_LOGIN_USERS、K6_VUS、K6_DURATION、K6_LOGIN_ITERS

import http from "k6/http";
import { check } from "k6";
import { Counter, Rate } from "k6/metrics";

const password = __ENV.K6_PASSWORD || "k6test123";
const nUsers = parseInt(__ENV.K6_LOGIN_USERS || "100", 10);//预注册用户数（默认 100 个）
const loginIters = parseInt(__ENV.K6_LOGIN_ITERS || "20", 10);//每轮迭代内登录次数（默认 20 次）
const vus = parseInt(__ENV.K6_VUS || "120", 10);//并发用户数（默认 120 个）
const duration = __ENV.K6_DURATION || "1m";//持续时间（默认 1 分钟）
const base = __ENV.BASE_URL || "http://localhost:8080";
const runTag = `${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;
const setupFailRate = new Rate("setup_reg_failed_rate");//注册失败率
const loginStatus0 = new Counter("login_status0_total");//登录成功次数
const loginStatus400 = new Counter("login_status400_total");//登录失败次数（业务失败）
const loginStatus500 = new Counter("login_status500_total");//登录失败次数（服务端错误）
const loginStatusOther = new Counter("login_status_other_total");//登录失败次数（其他状态）

export const options = {
  scenarios: {
    login_session: {
      executor: "constant-vus",//固定并发数
      vus,//并发用户数
      duration,//持续时间
    },
  },
  thresholds: {
    http_req_failed: ["rate<0.05"],
    http_req_duration: ["p(95)<200"],
    setup_reg_failed_rate: ["rate<0.2"],
  },
  noConnectionReuse: true,
};

const jsonHeaders = { "Content-Type": "application/json" };

//POST 请求并重试
function postWithRetry(url, body, params, maxRetries = 2) {
  let res = null;
  for (let attempt = 0; attempt <= maxRetries; attempt++) {
    res = http.post(url, body, params);
    const transientFail = res.status === 0 || !!res.error;
    if (!transientFail) return res;
  }
  return res;
}

//准备工作：预注册一批用户（全局只执行一次）
export function setup() {
  if (!/^https?:\/\/[^/]+/u.test(base)) {
    throw new Error(`invalid BASE_URL: raw="${rawBase}" sanitized="${base}"`);
  }
  const usernames = [];
  let regFailed = 0;
  for (let i = 1; i <= nUsers; i++) {
    const username = `k6_login_${i}_${runTag}`;//用户名
    usernames.push(username);
    const regRes = postWithRetry(
      `${base}/reg`,//url
      JSON.stringify({ username, password }),//body
      { headers: jsonHeaders/* 请求头 */ }
    );
    const ok = regRes.status === 200 || regRes.status === 400;
    check(regRes, { "setup reg ok or duplicate": () => ok });
    if (!ok) {
      regFailed++;
    }
  }
  setupFailRate.add(regFailed / nUsers);
  if (regFailed > Math.max(3, Math.floor(nUsers * 0.2))) {
    throw new Error(`setup register failed too many: ${regFailed}/${nUsers}`);
  }
  return { usernames };
}

//压测：登录（会话创建 + Redis setSession 写入路径，启用 Redis 时）
export default function (data) {
  if (__VU > nUsers) {
    return;
  }
  const username = data.usernames[__VU - 1];

  for (let i = 0; i < loginIters; i++) {
    const res = http.post(
      `${base}/login`,
      JSON.stringify({ username, password }),
      { headers: jsonHeaders, tags: { name: "POST /login" } }
    );
    if (res.status === 0) loginStatus0.add(1);
    else if (res.status === 400) loginStatus400.add(1);
    else if (res.status >= 500) loginStatus500.add(1);
    else if (res.status !== 200) loginStatusOther.add(1);
    check(res, { "login 200": (r) => r.status === 200 });
  }
}
