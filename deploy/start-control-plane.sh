#!/bin/sh
set -eu

target_directory=/run/hvc-secrets
install -d -m 0700 -o hvc -g hvc "${target_directory}"

copy_secret() {
    name=$1
    source="/run/secrets/${name}"
    if [ -f "${source}" ]; then
        install -m 0400 -o hvc -g hvc "${source}" "${target_directory}/${name}"
    fi
}

copy_secret hvc-identities
copy_secret livekit-api-secret

exec setpriv --reuid=hvc --regid=hvc --init-groups \
    /usr/local/bin/hvc-control-plane "$@"
