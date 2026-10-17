import asyncio
import os
import signal

import pytest
from .playerctl import stop_all


@pytest.fixture(autouse=True)
async def cleanup_processes():
    yield
    await stop_all()


@pytest.fixture()
async def bus_address():
    proc = await asyncio.create_subprocess_shell(
        'dbus-launch', stdout=asyncio.subprocess.PIPE
    )
    stdout, __ = await proc.communicate()
    await proc.wait()
    assert proc.returncode == 0
    address = None
    pid = None
    for line in stdout.decode().split():
        if line.startswith('DBUS_SESSION_BUS_ADDRESS='):
            address = line.split('=', 1)[1].strip()
        elif line.startswith('DBUS_SESSION_BUS_PID='):
            pid = int(line.split('=', 1)[1])

    assert address

    yield address

    # dbus-launch daemonizes the bus, so it has to be stopped explicitly
    if pid is not None:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
