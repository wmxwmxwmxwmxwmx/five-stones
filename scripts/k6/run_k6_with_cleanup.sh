#!/usr/bin/env bash
# 运行 k6，退出时（成功或失败）自动执行 cleanup_k6_mysql.sh 清理压测用户。
# 用法：./scripts/k6/run_k6_with_cleanup.sh run scripts/k6/info_user_cache.js
#       或任意 k6 子命令：./scripts/k6/run_k6_with_cleanup.sh inspect scripts/k6/info_user_cache.js
# 注意：进程被 SIGKILL 杀死时不会触发清理；可事后手动执行 cleanup_k6_mysql.sh。
# K6_SKIP_MYSQL_CLEANUP=1 可跳过清理。

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

run_cleanup() {
  bash "$SCRIPT_DIR/cleanup_k6_mysql.sh" || echo "run_k6_with_cleanup.sh: cleanup failed (exit $?)" >&2
}

trap run_cleanup EXIT

set +e
k6 "$@"
ec=$?
set -e
exit "$ec"
