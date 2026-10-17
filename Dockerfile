ARG UBUNTU_VERSION=24.04
ARG UV_VERSION=0.8.0

FROM ghcr.io/astral-sh/uv:${UV_VERSION} AS uv

FROM ubuntu:${UBUNTU_VERSION}

ARG PREFIX=/usr

WORKDIR /app

RUN export DEBIAN_FRONTEND=noninteractive; \
    export DEBCONF_NONINTERACTIVE_SEEN=true; \
    echo 'tzdata tzdata/Areas select Etc' | debconf-set-selections; \
    echo 'tzdata tzdata/Zones/Etc select UTC' | debconf-set-selections; \
    apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    ninja-build \
    build-essential \
    libglib2.0-dev \
    libgirepository1.0-dev \
    gtk-doc-tools \
    dbus \
    dbus-x11 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=uv /uv /usr/local/bin/uv
ENV UV_PYTHON_PREFERENCE=only-system
COPY pyproject.toml uv.lock ./
RUN uv sync --frozen
ENV PATH=/app/.venv/bin:$PATH

ADD . /app

COPY test/data/dbus-system.conf /etc/dbus-1/system.d/test-dbus-system.conf

RUN meson setup --prefix=${PREFIX} build && \
    ninja -C build && ninja -C build install
RUN mkdir -p /run/dbus
ENV PYTHONASYNCIODEBUG=1
ENV DBUS_SYSTEM_BUS_ADDRESS=unix:path=/var/run/dbus/system_bus_socket
CMD ["bash", "-c", "dbus-daemon --nopidfile --system && dbus-run-session python3 -m pytest -vvs"]
