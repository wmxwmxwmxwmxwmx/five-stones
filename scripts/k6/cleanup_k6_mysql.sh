#!/usr/bin/env bash
# 删除 k6 压测写入的 user 行：用户名匹配 k6_vu_* 与 k6_login_*（见 info_user_cache.js / login_session_write.js）。
# 依赖：本机已安装 mysql 客户端（mysql-client / mariadb-client）。
# 环境变量与 gobang_server 一致：MYSQL_HOST、MYSQL_USER、MYSQL_PASSWORD、MYSQL_DATABASE、MYSQL_PORT
# 设置 K6_SKIP_MYSQL_CLEANUP=1 则跳过（不执行 DELETE）。

set -euo pipefail

if [[ "${K6_SKIP_MYSQL_CLEANUP:-}" == "1" ]]; then
  echo "cleanup_k6_mysql.sh: skipped (K6_SKIP_MYSQL_CLEANUP=1)"
  exit 0
fi

if ! command -v mysql >/dev/null 2>&1; then
  echo "cleanup_k6_mysql.sh: mysql 客户端未找到，请安装 mysql-client 或 mariadb-client" >&2
  exit 1
fi

MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_USER="${MYSQL_USER:-wmx}"
MYSQL_PASSWORD="${MYSQL_PASSWORD:-123456}"
MYSQL_DATABASE="${MYSQL_DATABASE:-online_gobang}"
MYSQL_PORT="${MYSQL_PORT:-3306}"

# 与 k6 脚本一致：仅删除压测用户；REGEXP 需 MySQL 5.7+ / MariaDB 10.0+
# 兼容写法（无 REGEXP）：DELETE FROM user WHERE username LIKE 'k6\\_vu\\_%' OR username LIKE 'k6\\_login\\_%' ESCAPE '\\';

deleted="$(
  mysql -h"$MYSQL_HOST" -P"$MYSQL_PORT" -u"$MYSQL_USER" -p"$MYSQL_PASSWORD" \
    --default-character-set=utf8mb4 -N \
    -e "USE \`$MYSQL_DATABASE\`; DELETE FROM user WHERE username REGEXP '^k6_(vu|login)_'; SELECT ROW_COUNT();"
)"

echo "cleanup_k6_mysql.sh: deleted_rows=${deleted}"
