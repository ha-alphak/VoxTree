#!/bin/sh
set -eu

delay_ms=${HVC_NETEM_DELAY_MS:-0}
loss_percent=${HVC_NETEM_LOSS_PERCENT:-0}

case "${delay_ms}" in
    ''|*[!0-9]*) echo "HVC_NETEM_DELAY_MS must be a non-negative integer." >&2; exit 2 ;;
esac
case "${loss_percent}" in
    ''|*[!0-9.]*|.*|*.) echo "HVC_NETEM_LOSS_PERCENT must be a non-negative number." >&2; exit 2 ;;
esac

if [ "${delay_ms}" != "0" ] || [ "${loss_percent}" != "0" ]; then
    tc qdisc replace dev eth0 root netem \
        delay "${delay_ms}ms" \
        loss "${loss_percent}%"
fi

exec "$@"
