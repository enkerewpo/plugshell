# SPDX-License-Identifier: AGPL-3.0-or-later
# Convenience wrapper around CMake. CMake remains the source of truth.

BUILD_DIR ?= build
BUILD_TYPE ?= RelWithDebInfo
GENERATOR ?= Ninja
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

SOURCES := $(shell find src spike -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) 2>/dev/null)

.DEFAULT_GOAL := help

.PHONY: help
help: ## Show this help
	@grep -hE '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) \
		| awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-14s\033[0m %s\n", $$1, $$2}'

.PHONY: deps
deps: ## Fetch vendored dependencies (JUCE)
	git submodule update --init --recursive --depth 1

.PHONY: configure
configure: ## Configure the CMake build tree
	cmake -B $(BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

.PHONY: build
build: configure ## Build everything
	cmake --build $(BUILD_DIR) -j $(JOBS)

.PHONY: spike
spike: build ## Build and run the phase-1 feasibility spike
	$(BUILD_DIR)/spike/plugshell_spike

.PHONY: format
format: ## Rewrite sources with clang-format
	@test -n "$(SOURCES)" && clang-format -i $(SOURCES) || echo "no sources yet"

.PHONY: format-check
format-check: ## Fail if sources are not formatted
	@test -n "$(SOURCES)" && clang-format --dry-run --Werror $(SOURCES) || echo "no sources yet"

.PHONY: license-check
license-check: ## Fail if any source lacks an SPDX identifier
	@bash tools/check-spdx.sh

.PHONY: check
check: format-check license-check ## Run all static checks

.PHONY: clean
clean: ## Remove the build tree
	rm -rf $(BUILD_DIR)
