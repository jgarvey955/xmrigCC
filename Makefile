.PHONY: release-static

release-static:
	cmake -E remove_directory "$(CURDIR)/build-static"
	BUILD_DIR="$(CURDIR)/build-static" ./build-static.sh
