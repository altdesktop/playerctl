FROM ubuntu:20.04

WORKDIR /app

RUN export DEBIAN_FRONTEND=noninteractive; \
    export DEBCONF_NONINTERACTIVE_SEEN=true; \
    echo 'tzdata tzdata/Areas select Etc' | debconf-set-selections; \
    echo 'tzdata tzdata/Zones/Etc select UTC' | debconf-set-selections; \
    apt update && apt install -y --no-install-recommends \
    python3-pip \
    ninja-build \
    build-essential \
    libglib2.0-dev \
    libgirepository1.0-dev \
    gtk-doc-tools \
    dbus-x11

COPY requirements.txt .
RUN pip3 install -r requirements.txt

ADD . /app

COPY test/data/dbus-system.conf /etc/dbus-1/system.d/test-dbus-system.conf

RUN meson --prefix=/usr build && \
    ninja -C build && ninja -C build install
RUN mkdir -p /run/dbus
ENV PYTHONASYNCIODEBUG=1
ENV DBUS_SYSTEM_BUS_ADDRESS=unix:path=/var/run/dbus/system_bus_socket
CMD ["bash", "-c", "dbus-daemon --nopidfile --system && dbus-run-session python3 -m pytest -vvs"]
