# SPDX-License-Identifier: GPL-3.0-or-later
# PS5-ECAM build. `make help` lists the targets. Missing dependencies are fetched at their pinned
# revisions (tools/deps.json); nothing that already exists is changed. The build runs on Linux with
# clang-18, lld-18 and the LLVM 18 tools, cmake, ninja, git, python3 and make.

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := help
MAKEFLAGS += --no-print-directory

# The release's version, set here only: port/CMakeLists.txt and tools/package.sh (param.json) read it
VERSION := 5.1.1
APP := build/app/PPSA99360
JOBS ?= $(shell nproc)
export JOBS

.PHONY: retro test-retro help deps deps-status radv azahar melonds eden build check package release clean distclean test-contribution

test-retro: ## Host verification for the additional system integration
	bash tools/tests/test-retro.sh

test-contribution: ## Host checks for standalone integrations, transfers, Slot-2 and capture (no console)
	python3 -B tools/tests/test-performance.py
	python3 -B tools/tests/test-performance-suite.py
	bash tools/tests/test-eden-extras.sh
	bash tools/tests/test-profile-management.sh
	bash tools/tests/test-save-transfer.sh
	bash tools/tests/test-saves.sh
	bash tools/tests/test-slot2.sh
	bash tools/tests/test-microphone.sh
	bash tools/tests/test-frame-capture.sh

help: ## List the targets
	@echo 'PS5-ECAM build - make [target] [VARIABLE=value]'
	@echo
	@awk 'BEGIN { FS = ":.*## " } /^[a-z][a-z-]*:.*## / { printf "  %-12s %s\n", $$1, $$2 }' $(MAKEFILE_LIST)
	@echo
	@echo 'Variables:'
	@echo '  JOBS=$(JOBS)                parallel compile jobs'
	@echo '  RADV_PATCHES=0006,0007,0008,0009'
	@echo '                              the driver patches (patches/mesa) RADV is built with: their numbers,'
	@echo '                              all (0001-0007), or empty for none (0006: 120 Hz output after the new'
	@echo '                              launcher; 0007: no GPU fault on a buffer freed while the GPU still'
	@echo '                              uses it; 0008: submissions on a thread of their own, asyncSubmit;'
	@echo '                              0009: the GPU waits sleep gpuWaitSleepUs)'
	@echo '  RADV_ARCHIVE, RADV_SDK      a RADV built elsewhere, and the SDK fork it was built with'

deps: ## Fetch the pinned inputs and build the libraries Cemu needs for the PS5
	python3 -B tools/deps.py fetch
	bash tools/build-deps.sh

deps-status: ## Show every dependency and whether it matches its pin
	@python3 -B tools/deps.py status

radv: deps ## Build RADV, the Vulkan driver, with PS5_Vulkan's recipe and patches/mesa (RADV_PATCHES)
	bash tools/build-radv.sh

azahar: deps ## Build Azahar's core (the 3DS side) and its PS5 frontend: build/azahar
	bash tools/build-azahar.sh

melonds: deps ## Build melonDS's core (the DS side's) and its PS5 frontend: build/melonds
	bash tools/build-melonds.sh

eden: deps ## Build ProsperoEdenX, the Switch side's program (Eden), with patches/eden: build/eden
	bash tools/build-eden.sh

build: deps azahar melonds $(if $(RADV_ARCHIVE),,radv) ## Build PS5-ECAM (Cemu, Azahar and melonDS) and link it with RADV: build/cemu/ps5cemu.elf
	bash tools/build-cemu.sh

check: deps azahar melonds ## Build and package everything with a stand-in for RADV, to check the build (not an app)
	PS5CEMU_LINK_CHECK=1 bash tools/build-cemu.sh
	bash tools/package.sh --check

retro: ## Build the pinned Porpoise, N64 and NES runtimes (prepared donor toolchains required)
	python3 tools/build-retro.py porpoise n64 nes

package: build eden ## The app folder: build/app/PPSA99360 (with eden/, the Switch side's program)
	bash tools/package.sh

# a release takes the emulators' patches only as they are: a checkout in .deps with commits not exported
# or changes not committed stops it (ps5cemu_patches_apply in tools/env.sh)
release: export PS5CEMU_RELEASE := 1
release: package ## dist/PS5-ECAM-vVERSION.zip (the PPSA99360 folder), SHA256SUMS, and the ELF with its symbols
	@mkdir -p dist
	rm -f dist/PS5-ECAM-v$(VERSION).zip
	cd build/app && python3 -m zipfile -c ../../dist/PS5-ECAM-v$(VERSION).zip PPSA99360
	cd dist && sha256sum PS5-ECAM-v$(VERSION).zip > SHA256SUMS
	@# the unstripped ELF of the eboot, kept to look up this release's crash reports
	@# (tools/symbolize-crash.py dist/ps5cemu-vVERSION.elf boot.log)
	cp build/cemu/ps5cemu.elf dist/ps5cemu-v$(VERSION).elf
	@# and the Switch side's program's (ProsperoEdenX's tools/symbolize-crash.py)
	if [[ -f build/eden/eden.elf ]]; then cp build/eden/eden.elf dist/eden-v$(VERSION).elf; fi
	@cat dist/SHA256SUMS

clean: ## Remove build outputs (build/, dist/); dependencies stay
	rm -rf build dist

distclean: clean ## Also remove the fetched dependencies in .deps
	python3 -B tools/deps.py clean
