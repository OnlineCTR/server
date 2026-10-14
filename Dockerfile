# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2024-2026 OnlineCTR Team.
# Licensed under the GNU AGPL-3.0 (LICENSE), with the additional terms in
# ADDITIONAL-TERMS under its section 7. ABSOLUTELY NO WARRANTY.

# OnlineCTR server in a container: docker compose up --build (compose.yml).
# The server's settings come from octr-server.env at run time (compose
# env_file; see octr-server.env.example) - never from the image.
FROM i386/debian:bookworm-slim AS builder

RUN apt-get update \
 && apt-get install -y --no-install-recommends cmake build-essential libcurl4-openssl-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src
RUN cmake -S . -B build && cmake --build build

# The runtime is a slim base with libcurl's runtime packages installed rather
# than FROM scratch: libcurl drags in libssl, libcrypto, libz, libnghttp2,
# libidn2, libpsl and more, and TLS verification needs a CA bundle that would
# otherwise never receive updates. ENet is linked in (bundled, enet/).
FROM i386/debian:bookworm-slim AS final

RUN apt-get update \
 && apt-get install -y --no-install-recommends libcurl4 ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /src/build/ctr_srv /app/

# Nothing here needs root: the socket is an unprivileged port and the binary
# is read-only. A hole in the server must not hand out a root shell.
RUN useradd --system --no-create-home --shell /usr/sbin/nologin octr
USER octr

EXPOSE 64001/udp
ENTRYPOINT ["/app/ctr_srv", "-p", "64001"]
