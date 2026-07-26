FROM debian:13-slim AS build

RUN apt-get update \
    && apt-get install --no-install-recommends -y build-essential cmake libsqlite3-dev ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DHVC_BUILD_TESTS=OFF \
      -DHVC_BUILD_EXAMPLES=OFF \
      -DHVC_WARNINGS_AS_ERRORS=ON \
    && cmake --build /build --target hvc_load_driver

FROM livekit/livekit-cli@sha256:de926d86d1744c8afaa975f126f7304c5b2e13e8e20aaba8cba979303ce1fe03 AS livekit_cli

FROM debian:13-slim

RUN apt-get update \
    && apt-get install --no-install-recommends -y ca-certificates iproute2 libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /build/apps/load-driver/hvc-load-driver /usr/local/bin/hvc-load-driver
COPY --from=livekit_cli /lk /usr/local/bin/lk
COPY deploy/start-load-generator.sh /usr/local/bin/start-load-generator.sh
RUN chmod 0555 /usr/local/bin/start-load-generator.sh

ENTRYPOINT ["/usr/local/bin/start-load-generator.sh"]
CMD ["hvc-load-driver", "--help"]
