#!/bin/sh
set -eu

api_secret="$(cat /run/secrets/livekit-api-secret)"
exec /livekit-server --config /etc/livekit.yaml --keys "hvc: ${api_secret}"
