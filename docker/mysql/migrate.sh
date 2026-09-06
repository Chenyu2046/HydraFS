#!/bin/bash
set -euo pipefail

MYSQL_HOST=${MYSQL_HOST:-tc_mysql}
MYSQL_USER=${MYSQL_USER:-yuncunchu}
MYSQL_DATABASE=${MYSQL_DATABASE:-yuncunchu}
MYSQL_PWD=${MYSQL_PWD:-123456}
export MYSQL_PWD

for attempt in $(seq 1 60); do
    if mysqladmin ping -h "$MYSQL_HOST" -u "$MYSQL_USER" --silent; then
        break
    fi
    if [ "$attempt" -eq 60 ]; then
        echo "MySQL did not become ready" >&2
        exit 1
    fi
    sleep 2
done

last_v2_checksum=''
last_ai_checksum=''
while true; do
    v2_checksum=$(sha256sum /migrations/hydra_v2.sql | awk '{print $1}')
    ai_checksum=$(sha256sum /migrations/hydra_ai_v2.sql | awk '{print $1}')
    if [ "$v2_checksum" != "$last_v2_checksum" ]; then
        rm -f /tmp/hydrastore-v2-migration-ready
        mysql --init-command="SET @hydrastore_v2_checksum='${v2_checksum}'" \
            -h "$MYSQL_HOST" -u "$MYSQL_USER" "$MYSQL_DATABASE" < /migrations/hydra_v2.sql
        last_v2_checksum="$v2_checksum"
        touch /tmp/hydrastore-v2-migration-ready
        echo "HydraStore V2 migration complete: $v2_checksum"
    fi
    if [ "$ai_checksum" != "$last_ai_checksum" ]; then
        rm -f /tmp/hydrastore-ai-v2-migration-ready
        mysql --init-command="SET @hydrastore_ai_v2_checksum='${ai_checksum}'" \
            -h "$MYSQL_HOST" -u "$MYSQL_USER" "$MYSQL_DATABASE" < /migrations/hydra_ai_v2.sql
        last_ai_checksum="$ai_checksum"
        touch /tmp/hydrastore-ai-v2-migration-ready
        echo "HydraStore AI V2 migration complete: $ai_checksum"
    fi
    sleep 10
done
