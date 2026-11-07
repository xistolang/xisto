# Test image for the Linux port: compiler toolchain plus what tests/linux/run.sh needs.
FROM debian:12-slim
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends build-essential xxd python3 curl wrk ca-certificates sqlite3 procps cmake ninja-build openssl perl tzdata && rm -rf /var/lib/apt/lists/*
WORKDIR /work
