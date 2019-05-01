.PHONY: test
.DEFAULT_TARGET := test

test:
	dbus-run-session python3 -m pytest -sq

docker-test:
	docker build -t playerctl-test .
	docker run -it playerctl-test

format:
	yapf -rip t
