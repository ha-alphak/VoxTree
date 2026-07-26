FROM livekit/livekit-server:v1.13.4 AS upstream

FROM alpine:3.22.1
RUN apk add --no-cache ca-certificates
COPY --from=upstream /livekit-server /livekit-server
COPY deploy/start-livekit.sh /usr/local/bin/start-livekit.sh
ENTRYPOINT ["/bin/sh", "/usr/local/bin/start-livekit.sh"]
