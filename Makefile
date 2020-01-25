.PHONY: test docker-test format all
.DEFAULT_GOAL := all

FORMAT_C_SOURCE = $(shell find playerctl | grep \.[ch]$)
EXECUTABLES = clang-format python3 docker yapf dbus-run-session
K := $(foreach exec,$(EXECUTABLES),\
        $(if $(shell which $(exec)),some string,$(error "No $(exec) in PATH")))

test:
	dbus-run-session python3 -m pytest -sq

docker-test:
	docker build -t playerctl-test .
	docker run -it playerctl-test

format:
	yapf -rip test examples
	clang-format -i ${FORMAT_C_SOURCE}

lint:
	flake8 test

all: format docker-test
