FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      build-essential cmake git ca-certificates wget pkg-config libssl-dev \
      libgtest-dev && \
    # Build gtest static libs if not already installed via package
    if [ -d /usr/src/gtest ]; then \
      cmake -S /usr/src/gtest -B /usr/src/gtest/build && \
      cmake --build /usr/src/gtest/build && \
      cp /usr/src/gtest/build/libgtest*.a /usr/lib || true; \
    fi && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
