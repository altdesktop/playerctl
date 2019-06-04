FROM ubuntu:19.04

WORKDIR /app

RUN apt-get update && apt-get install -y \
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

RUN meson --prefix=/usr build && ninja -C build && ninja -C build install
CMD ["dbus-run-session", "python3", "-m", "pytest", "-s"]
