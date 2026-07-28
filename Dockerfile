FROM debian:13-slim AS build

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
      build-essential cmake libsqlite3-dev libudev-dev ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DHVC_BUILD_TESTS=OFF \
      -DHVC_BUILD_EXAMPLES=OFF \
      -DHVC_WARNINGS_AS_ERRORS=ON \
    && cmake --build /build --target hvc_control_plane

FROM debian:13-slim

RUN apt-get update \
    && apt-get install --no-install-recommends -y ca-certificates curl libsqlite3-0 util-linux \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system hvc \
    && useradd --system --gid hvc --home-dir /var/lib/hvc hvc \
    && install -d -o hvc -g hvc /var/lib/hvc

COPY --from=build /build/apps/control-plane/hvc-control-plane /usr/local/bin/hvc-control-plane
COPY deploy/start-control-plane.sh /usr/local/bin/start-control-plane.sh
RUN chmod 0555 /usr/local/bin/start-control-plane.sh

EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/start-control-plane.sh"]
