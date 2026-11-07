# vfdsvc Makefile — cross-builds static binaries in docker (reproducible).
VERSION := 0.1.1
IMG     := ubuntu:18.04
PKGS    := gcc libc6-dev build-essential make file \
           gcc-arm-linux-gnueabihf libc6-dev-armhf-cross gcc-aarch64-linux-gnu libc6-dev-arm64-cross

.PHONY: armv7 arm64 all clean dist docker-build docker-smoke

all: armv7 arm64

armv7:
	docker run --rm -v "$(CURDIR)":/v $(IMG) bash -ec '\
	  apt-get update -qq && \
	  apt-get install -y -qq --no-install-recommends $(PKGS) >/dev/null && cd /v && mkdir -p /v/dist && \
	  arm-linux-gnueabihf-gcc -s -static -O2 -Wall -Wextra \
	    -DVFD_VERSION="\"vfdsvc $(VERSION)\"" \
	    -o /v/dist/vfdsvc-armv7 src/vfdsvc.c && \
	  file /v/dist/vfdsvc-armv7'

arm64:
	docker run --rm -v "$(CURDIR)":/v $(IMG) bash -ec '\
	  apt-get update -qq && \
	  apt-get install -y -qq --no-install-recommends $(PKGS) >/dev/null && cd /v && mkdir -p /v/dist && \
	  aarch64-linux-gnu-gcc -s -static -O2 -Wall -Wextra \
	    -DVFD_VERSION="\"vfdsvc $(VERSION)\"" \
	    -o /v/dist/vfdsvc-arm64 src/vfdsvc.c && \
	  file /v/dist/vfdsvc-arm64'

clean:
	rm -rf dist

dist: all
	@echo "artifacts in dist/"
