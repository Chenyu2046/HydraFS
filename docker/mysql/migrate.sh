#!/bin/bash
set -euo pipefail

MYSQL_HOST=${MYSQL_HOST:-tc_mysql}
MYSQL_USER=${MYSQL_USER:-yuncunchu}
MYSQL_DATABASE=${MYSQL_DATABASE:-yuncunchu}
MYSQL_PWD=${MYSQL_PWD:-123456}

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

export MYSQL_PWD
last_checksum=''
while true; do
    checksum=$(sha256sum /migrations/hydra_v2.sql | awk '{print $1}')
    if [ "$checksum" != "$last_checksum" ]; then
        mysql -h "$MYSQL_HOST" -u "$MYSQL_USER" "$MYSQL_DATABASE" < /migrations/hydra_v2.sql
        last_checksum="$checksum"
        touch /tmp/hydrastore-v2-migration-ready
        echo "HydraStore V2 migration complete: $checksum"
    fi
    sleep 10
done
