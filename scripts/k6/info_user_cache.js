// 压测 GET /info（用户读缓存热路径）：setup 预注册并登录，default 仅发 /info。
// 依赖：BASE_URL；可选 K6_PASSWORD、K6_VUS、K6_DURATION、K6_INFO_ITERS
//
// Redis 相关：启用服务端 Redis 时，首次 select_by_id 可能 miss 后回填；后续命中用户缓存。
// 对比实验：同一命令下分别启动 FIVE_STONES_WITH_REDIS=OFF / ON 的服务端。

import http from "k6/http";
import { check } from "k6";

const password = __ENV.K6_PASSWORD || "k6test123";//密码
const infoIters = parseInt(__ENV.K6_INFO_ITERS || "50", 10);//每轮迭代内 /info 请求次数（从 k6 的环境变量中读取一个字符串，如果未设置则使用默认值 20）
const vus = parseInt(__ENV.K6_VUS || "120", 10);//并发用户数（默认 120 个）
const duration = __ENV.K6_DURATION || "1m";//持续时间（默认 1 分钟）
const base = __ENV.BASE_URL || "http://localhost:8080";
//在持续时间内维持 vus 个虚拟用户；每个 VU 会不断重复执行 export default function () { ... }，直到时间到。
export const options = {
  scenarios: {
    info_user_cache: {
      executor: "constant-vus",//固定并发数
      vus,//并发用户数
      duration,//持续时间
    },
  },
  thresholds: {
    http_req_failed: ["rate<0.05"],
    http_req_duration: ["p(95)<200"],
    "http_req_failed{name:GET /info}": ["rate<0.01"],
    "http_req_duration{name:GET /info}": ["p(95)<200"],
  },
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

//提取 Set-Cookie 头
function pickSetCookie(res) {
  const h = res.headers["Set-Cookie"];
  if (!h) return "";
  return Array.isArray(h) ? h[0] : h;
}

//准备工作：预注册并登录一批用户（全局只执行一次）
export function setup() {
  if (!/^https?:\/\/[^/]+/u.test(base)) {
    throw new Error(`invalid BASE_URL: raw="${rawBase}" sanitized="${base}"`);
  }
  const cookies = [];
  const runTag = `${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;
  for (let i = 1; i <= vus; i++) {
    const username = `k6_vu_${i}_${runTag}`;
    let res = postWithRetry(
      `${base}/reg`,
      JSON.stringify({ username, password }),
      { headers: jsonHeaders, tags: { name: "POST /reg (setup)" } }
    );
    check(res, { "setup reg ok or duplicate": (r) => r.status === 200 || r.status === 400 });
    res = postWithRetry(
      `${base}/login`,
      JSON.stringify({ username, password }),
      { headers: jsonHeaders, tags: { name: "POST /login (setup)" } }
    );
    if (res.status !== 200) {
      throw new Error(`setup login failed for ${username}, status=${res.status}`);
    }
    const cookie = pickSetCookie(res);
    if (!cookie) {
      throw new Error(`setup login missing Set-Cookie for ${username}`);
    }
    cookies.push(cookie);
  }
  return { cookies };
}

//压测：读取用户信息（用户读缓存热路径）
export default function (data) {
  const idx = __VU - 1;
  const cookie = data.cookies[idx];
  if (!cookie) {
    throw new Error(`missing cookie for VU ${__VU}`);
  }
  for (let i = 0; i < infoIters; i++) {
    const res = http.get(`${base}/info`, {
      headers: { Cookie: cookie },
      tags: { name: "GET /info" },
    });
    check(res, { "info 200": (r) => r.status === 200 });
  }
}
