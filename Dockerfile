# ── Build & test ──────────────────────────────────────────────────────────────
# NOTE: The main watchmooi binary requires a display (libmpv / GPU).
#       Docker can only run the test suite (no GUI).
#
#   Build + test:   docker build --target test -t watchmooi-test .
#   Build only:     docker build --target builder -t watchmooi-builder .

FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake make g++ libmpv-dev libcurl4-openssl-dev pkg-config git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY . /src
WORKDIR /src/build
RUN cmake .. && make -j$(nproc)

# ── Test stage ────────────────────────────────────────────────────────────────

FROM builder AS test
RUN ./watchmooi_tests
