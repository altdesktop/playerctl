import pytest
import asyncio


@pytest.fixture()
async def bus_address(scope='class'):
    proc = await asyncio.create_subprocess_shell(
        'dbus-launch', stdout=asyncio.subprocess.PIPE)
    stdout, __ = await proc.communicate()
    await proc.wait()
    assert proc.returncode == 0
    address = None
    for line in stdout.decode().split():
        if line.startswith('DBUS_SESSION_BUS_ADDRESS='):
            address = line.split('=', 1)[1].strip()
            break

    assert address

    return address
