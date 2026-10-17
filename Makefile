.PHONY: help build test test-local test-container docker-test all \
	format format-py format-c \
	format-check format-check-py format-check-c \
	lint lint-py lint-c lint-sh
.DEFAULT_GOAL := help

FORMAT_C_SOURCE = $(shell find playerctl | grep \.[ch]$)
PY_DIRS = test examples
SH_SOURCE = fpm-packages.sh data/playerctl.bash
help: ## show this help
	@awk 'BEGIN {FS = ":.*## "; print "Usage: make [target]\n\nTargets:"} /^[a-zA-Z_-]+:.*## / {printf "  %-18s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

# Container engine: podman if available, else docker (override with CONTAINER_ENGINE=...)
CONTAINER_ENGINE ?= $(firstword $(notdir $(shell which podman docker 2>/dev/null)))

# Fail with a clear message when a tool is missing: $(call require,tool...)
require = @for t in $(1); do command -v $$t >/dev/null || { echo "No $$t in PATH" >&2; exit 1; }; done

# The tests run plain `playerctl`/`playerctld` from PATH, so put the meson build
# first to avoid silently testing a system-wide install.
BUILD_BIN = $(CURDIR)/build/playerctl

build: ## build playerctl into ./build (meson + ninja, needs glib dev packages)
	$(call require,uv)
	@test -f build/build.ninja || uv run meson setup build
	uv run meson compile -C build

test-local: ## run the pytest suite against ./build on the host (needs dbus-run-session; see test-container)
	$(call require,uv dbus-run-session)
	@test -x $(BUILD_BIN)/playerctl -a -x $(BUILD_BIN)/playerctld || { \
		echo "No built playerctl in $(BUILD_BIN): run 'make build' (needs glib dev packages) or use 'make test-container'" >&2; \
		exit 1; }
	PATH=$(BUILD_BIN):$$PATH dbus-run-session uv run pytest -sq

test-container: ## build the image and run the tests in a container (podman or docker)
	$(call require,$(CONTAINER_ENGINE))
	$(CONTAINER_ENGINE) build -t playerctl-test .
	$(CONTAINER_ENGINE) run -it playerctl-test

test: test-container ## run the tests in a container (alias for test-container)

# docker-test kept as an alias for test-container
docker-test: test-container ## alias for test-container

# Formatting: rewrite files in place
format: format-py format-c ## format Python and C sources in place

format-py: ## format Python sources in place (ruff)
	$(call require,uv)
	uv run ruff format $(PY_DIRS)

format-c: ## format C sources in place (clang-format)
	$(call require,clang-format)
	clang-format -i ${FORMAT_C_SOURCE}

# Formatting: fail on violations without changing files
format-check: format-check-py format-check-c ## check Python and C formatting without changing files

format-check-py: ## check Python formatting (ruff)
	$(call require,uv)
	uv run ruff format --check $(PY_DIRS)

format-check-c: ## check C formatting (clang-format)
	$(call require,clang-format)
	clang-format --dry-run --Werror ${FORMAT_C_SOURCE}

# Linting
lint: lint-py lint-c lint-sh ## lint Python, C and shell sources

lint-py: ## lint Python sources (ruff)
	$(call require,uv)
	uv run ruff check $(PY_DIRS)

# clang-tidy needs a compile database, which meson generates (this needs the
# glib/gobject-introspection dev packages, see the Dockerfile)
build/compile_commands.json:
	$(call require,uv)
	uv run meson setup build

lint-c: build/compile_commands.json ## lint C sources (clang-tidy, needs a meson build dir)
	$(call require,clang-tidy)
	clang-tidy -p build --quiet $(filter %.c,${FORMAT_C_SOURCE})

lint-sh: ## lint shell scripts (shellcheck)
	$(call require,shellcheck)
	shellcheck $(SH_SOURCE)

all: format-check lint test ## check formatting, lint, then run the container tests
