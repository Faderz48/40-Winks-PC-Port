ROM ?=
BUILD_DIR ?= build/recomp-port
JOBS ?= 2

.PHONY: all appimage bootstrap build clean distclean playable-appimage prepare run source-audit test

all: build

bootstrap:
	JOBS="$(JOBS)" tools/bootstrap_dependencies.sh

prepare: bootstrap
	@test -n "$(ROM)" || (echo "Set ROM=/path/to/your/clean-40-winks.z64"; exit 1)
	tools/prepare_recomp.sh "$(ROM)"

build: prepare
	cmake -S recomp-port -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release
	cmake --build "$(BUILD_DIR)" --parallel "$(JOBS)"

run: build
	./"$(BUILD_DIR)"/forty-winks-recomp --rom "$(ROM)"

test: build
	cmake --build "$(BUILD_DIR)" --parallel "$(JOBS)" --target \
		controller-pak-test debug-level-test input-routing-test split-screen-patch-test
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure

playable-appimage: prepare
	JOBS="$(JOBS)" packaging/linux/recomp-appimage/build_appimage.sh

appimage:
	packaging/linux/rom-free-appimage/build_appimage.sh

source-audit:
	tools/check_public_tree.sh

clean:
	rm -rf build dist recomp/generated recomp/baserom.z64

distclean: clean
	rm -rf work/external/N64ModernRuntime work/external/N64Recomp work/external/rt64
