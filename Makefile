# vfdsvc Makefile — cross-builds static binaries in docker (reproducible).
VERSION := 0.1.6
IMG     := ubuntu:18.04
PKGS    := gcc libc6-dev build-essential make file \
           gcc-arm-linux-gnueabihf libc6-dev-armhf-cross gcc-aarch64-linux-gnu libc6-dev-arm64-cross

.PHONY: armv7 arm64 all clean dist docker-build docker-smoke bat-test

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

# Local smoke-test for the Windows installer .bat: wine cmd inside a
# throwaway container. Old .bat revisions must produce a parse failure,
# so the test can actually detect the bug class it guards against.
bat-test:
	docker run --rm -v "$(CURDIR)":/w -e HOME=/tmp -e WINEDEBUG=-all -e LC_ALL=C \
	  debian:bookworm bash -ec '\
	  apt-get update -qq >/dev/null && \
	  apt-get install -y -qq --no-install-recommends wine64 >/dev/null && \
	  wineboot -i >/dev/null 2>&1; cd /w && \
	  printf "Q\n" | wine64 cmd /c "Z:\\w\\scripts\\install_windows.bat" > /tmp/menu.log 2>&1; \
	  printf "1\n" | wine64 cmd /c "Z:\\w\\scripts\\install_windows.bat 192.168.88.1" > /tmp/ip.log 2>&1; \
	  if grep -qi unexpected /tmp/menu.log /tmp/ip.log; then echo BAT-TEST-FAIL:unexpected; cat /tmp/menu.log /tmp/ip.log; exit 1; fi; \
	  grep -q "adb.exe not found" /tmp/ip.log || { echo BAT-TEST-FAIL:no-adb-branch; cat /tmp/ip.log; exit 1; }; \
	  echo BAT-TEST-OK'
