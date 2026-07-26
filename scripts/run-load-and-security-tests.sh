#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
result_directory=${HVC_LOAD_RESULT_DIRECTORY:-"${repository}/out/load-tests/${timestamp}"}
project=${HVC_LOAD_PROJECT:-"hvc-load-$(date -u +%Y%m%d%H%M%S)"}
compose_files="-f compose.yaml -f deploy/compose.load-test.yaml"
temporary_directory=$(mktemp -d)
data_directory="${temporary_directory}/data"
identities_file="${temporary_directory}/hvc-identities.tsv"
secret_file="${temporary_directory}/livekit-api-secret"
livekit_config_file="${temporary_directory}/livekit.yaml"
image=hvc-load-generator:section-10-2
group_players=${HVC_LOAD_GROUP_PLAYERS:-200}
duration=${HVC_LOAD_MEDIA_DURATION:-20s}
advertise_ip=${HVC_LOAD_ADVERTISE_IP:-$(hostname -I | awk '{print $1}')}
cleanup_required=true

cleanup() {
    if [ "${cleanup_required}" = true ]; then
        docker compose ${compose_files} -p "${project}" down --volumes --remove-orphans \
            >/dev/null 2>&1 || true
    fi
    rm -rf -- "${temporary_directory}"
}
trap cleanup EXIT INT TERM

measure_first_audio_subscription() {
    container=$1
    phase=$2
    started_ms=$3
    deadline=$(( $(date +%s) + 30 ))

    while ! docker logs "${container}" 2>&1 | grep -q 'subscribed to track'; do
        if [ "$(docker inspect --format '{{.State.Running}}' "${container}")" != true ]; then
            docker logs "${container}" >&2
            echo "Media load ended before the first remote audio subscription." >&2
            return 1
        fi
        if [ "$(date +%s)" -ge "${deadline}" ]; then
            echo "Timed out waiting for the first remote audio subscription." >&2
            return 1
        fi
        sleep 0.05
    done

    observed_ms=$(date +%s%3N)
    latency_ms=$((observed_ms - started_ms))
    printf '%s\t%s\n' "${phase}" "${latency_ms}" \
        >>"${result_directory}/audio-start-latency.tsv"
}

cd "${repository}"
mkdir -p "${data_directory}" "${result_directory}"
chmod 0777 "${data_directory}"
printf 'phase\tgenerator_to_first_remote_audio_subscription_ms\n' \
    >"${result_directory}/audio-start-latency.tsv"
umask 077
printf '%s\n' 'hvc-section-10-2-livekit-secret' >"${secret_file}"
case "${group_players}" in
    ''|*[!0-9]*) echo "HVC_LOAD_GROUP_PLAYERS must be an integer of at least 200." >&2; exit 2 ;;
esac
if [ "${group_players}" -lt 200 ]; then
    echo "HVC_LOAD_GROUP_PLAYERS must be an integer of at least 200." >&2
    exit 2
fi
case "${advertise_ip}" in
    ''|*[!0-9.]*) echo "HVC_LOAD_ADVERTISE_IP must be an IPv4 address." >&2; exit 2 ;;
esac
cat >"${livekit_config_file}" <<EOF
port: 7880
rtc:
  node_ip: ${advertise_ip}
  tcp_port: 17881
  udp_port: 17882
EOF

export HVC_LOAD_DATA_DIRECTORY="${data_directory}"
export HVC_LOAD_RESULT_DIRECTORY="${result_directory}"
export HVC_LOAD_IDENTITIES_FILE="${identities_file}"
export HVC_LOAD_LIVEKIT_SECRET_FILE="${secret_file}"
export HVC_LOAD_LIVEKIT_CONFIG_FILE="${livekit_config_file}"
printf 'ws://%s:17880\n' "${advertise_ip}" \
    >"${result_directory}/external-livekit-endpoint.txt"

docker build -f "${repository}/deploy/load-generator.Dockerfile" \
    -t "${image}" "${repository}"
docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "${data_directory}:/fixture-data" \
    --volume "${temporary_directory}:/fixture-secrets" \
    "${image}" \
    hvc-load-driver prepare \
    --database /fixture-data/control-plane.db \
    --identities /fixture-secrets/hvc-identities.tsv \
    --group-players "${group_players}"
chmod 0666 "${data_directory}/control-plane.db"
chmod 0444 "${identities_file}"

docker compose ${compose_files} -p "${project}" up -d livekit control-plane
attempt=0
until docker compose ${compose_files} -p "${project}" exec -T control-plane \
    curl --fail --silent http://127.0.0.1:8080/api/v1/health >/dev/null; do
    attempt=$((attempt + 1))
    if [ "${attempt}" -ge 60 ]; then
        echo "Control Plane did not become ready." >&2
        exit 1
    fi
    sleep 1
done

network="${project}_default"

gate_load_container="${project}-control-gate-load"
docker run -d --name "${gate_load_container}" --network "${network}" \
    --volume "${result_directory}:/results" \
    "${image}" \
    hvc-load-driver soak \
    --host control-plane \
    --group-players "${group_players}" \
    --duration 40 \
    --report /results/control-plane-gate-background.json >/dev/null

docker run --rm --network "${network}" \
    --volume "${result_directory}:/results" \
    "${image}" \
    hvc-load-driver run \
    --host control-plane \
    --group-players "${group_players}" \
    --report /results/control-plane.json
if [ "$(docker wait "${gate_load_container}")" != "0" ]; then
    docker logs "${gate_load_container}" >&2
    exit 1
fi
docker logs "${gate_load_container}" >"${result_directory}/control-plane-gate-background.log" 2>&1
docker rm "${gate_load_container}" >/dev/null

group_container="${project}-media-group"
group_started_ms=$(date +%s%3N)
docker run -d --name "${group_container}" --network "${network}" \
    --env LIVEKIT_URL=ws://livekit:7880 \
    --env LIVEKIT_API_KEY=hvc \
    --env LIVEKIT_API_SECRET="$(cat "${secret_file}")" \
    "${image}" \
    lk load-test \
    --room group:load-group \
    --duration "${duration}" \
    --audio-publishers 1 \
    --subscribers "${group_players}" \
    --identity-prefix hvc-group \
    --num-per-second 50 >/dev/null
measure_first_audio_subscription \
    "${group_container}" group-200 "${group_started_ms}"
if [ "$(docker wait "${group_container}")" != "0" ]; then
    docker logs "${group_container}" >&2
    exit 1
fi
docker logs "${group_container}" >"${result_directory}/media-group.log" 2>&1
docker rm "${group_container}" >/dev/null

scope_containers=
scope_index=0
while [ "${scope_index}" -lt 4 ]; do
    container="${project}-scope-${scope_index}"
    docker run -d --name "${container}" --network "${network}" \
        --env LIVEKIT_URL=ws://livekit:7880 \
        --env LIVEKIT_API_KEY=hvc \
        --env LIVEKIT_API_SECRET="$(cat "${secret_file}")" \
        "${image}" \
        lk load-test \
        --room "team:load-team-$(printf '%04d' "${scope_index}")" \
        --duration "${duration}" \
        --audio-publishers 1 \
        --subscribers 4 \
        --identity-prefix "hvc-team-${scope_index}" \
        --num-per-second 20 >/dev/null
    scope_containers="${scope_containers} ${container}"
    scope_index=$((scope_index + 1))
done
for container in ${scope_containers}; do
    docker wait "${container}" >/dev/null
    docker logs "${container}" >>"${result_directory}/media-independent-scopes.log" 2>&1
    docker rm "${container}" >/dev/null
done

combined_container="${project}-media-combined"
combined_started_ms=$(date +%s%3N)
docker run -d --name "${combined_container}" --network "${network}" \
    --env LIVEKIT_URL=ws://livekit:7880 \
    --env LIVEKIT_API_KEY=hvc \
    --env LIVEKIT_API_SECRET="$(cat "${secret_file}")" \
    "${image}" \
    lk load-test \
    --room group:combined-load \
    --duration 30s \
    --audio-publishers 2 \
    --subscribers "${group_players}" \
    --identity-prefix hvc-combined \
    --num-per-second 50 >/dev/null
measure_first_audio_subscription \
    "${combined_container}" simultaneous-speakers-200 "${combined_started_ms}"
sleep 10
control_plane_container=$(docker compose ${compose_files} -p "${project}" ps -q control-plane)
livekit_container=$(docker compose ${compose_files} -p "${project}" ps -q livekit)
docker stats --no-stream --format \
    '{{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}\t{{.PIDs}}' \
    "${control_plane_container}" "${livekit_container}" "${combined_container}" \
    >"${result_directory}/resources.tsv"
docker run --rm --network "${network}" \
    --volume "${result_directory}:/results" \
    "${image}" \
    hvc-load-driver soak \
    --host control-plane \
    --group-players "${group_players}" \
    --duration 15 \
    --report /results/control-plane-combined.json
docker wait "${combined_container}" >/dev/null
docker logs "${combined_container}" >"${result_directory}/media-combined.log" 2>&1
docker rm "${combined_container}" >/dev/null

docker run --rm --network "${network}" --cap-add NET_ADMIN \
    --env HVC_NETEM_DELAY_MS=150 \
    --env HVC_NETEM_LOSS_PERCENT=2 \
    --env LIVEKIT_URL=ws://livekit:7880 \
    --env LIVEKIT_API_KEY=hvc \
    --env LIVEKIT_API_SECRET="$(cat "${secret_file}")" \
    "${image}" \
    lk load-test \
    --room group:netem-load \
    --duration "${duration}" \
    --audio-publishers 2 \
    --subscribers 20 \
    --identity-prefix hvc-netem \
    --num-per-second 20 \
    >"${result_directory}/media-netem.log" 2>&1

outage_container="${project}-control-outage"
docker run -d --name "${outage_container}" --network "${network}" \
    --volume "${result_directory}:/results" \
    "${image}" \
    hvc-load-driver soak \
    --host control-plane \
    --group-players "${group_players}" \
    --duration 25 \
    --require-recovery \
    --report /results/control-plane-recovery.json >/dev/null
sleep 5
docker compose ${compose_files} -p "${project}" stop control-plane >/dev/null
sleep 2
docker compose ${compose_files} -p "${project}" start control-plane >/dev/null
if [ "$(docker wait "${outage_container}")" != "0" ]; then
    docker logs "${outage_container}" >&2
    exit 1
fi
docker logs "${outage_container}" >"${result_directory}/control-plane-recovery.log" 2>&1
docker rm "${outage_container}" >/dev/null

for clean_soak_report in \
    control-plane-gate-background.json \
    control-plane-combined.json; do
    if ! grep -q '"failed_requests":0' "${result_directory}/${clean_soak_report}"; then
        echo "Unexpected Control Plane errors occurred in ${clean_soak_report}." >&2
        exit 1
    fi
done
if ! grep -Eq "${group_players}/${group_players}.*0 \\(0%\\).*0" \
    "${result_directory}/media-group.log"; then
    echo "The configured group media gate did not receive every track without errors." >&2
    exit 1
fi
if [ "$(grep -Ec '4/4.*0 \(0%\).*0' \
    "${result_directory}/media-independent-scopes.log")" -ne 4 ]; then
    echo "One or more independent scope media gates failed." >&2
    exit 1
fi
combined_tracks=$((group_players * 2))
if ! grep -Eq "${combined_tracks}/${combined_tracks}.*0 \\(0%\\).*0" \
    "${result_directory}/media-combined.log"; then
    echo "The simultaneous-speaker combined media gate failed." >&2
    exit 1
fi
if ! grep -Eq '40/40.*0 \(0%\).*0' "${result_directory}/media-netem.log"; then
    echo "The impaired-network media gate failed." >&2
    exit 1
fi

docker compose ${compose_files} -p "${project}" logs --no-color \
    >"${result_directory}/services.log"
docker compose ${compose_files} -p "${project}" down --volumes --remove-orphans
cleanup_required=false
rm -rf -- "${temporary_directory}"
trap - EXIT INT TERM

printf 'PASS: section 10.2 load run completed; reports: %s\n' "${result_directory}"
