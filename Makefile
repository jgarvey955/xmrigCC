.PHONY: release-static

release-static:
	BUILD_DIR="$(CURDIR)/build" ./build-static.sh
