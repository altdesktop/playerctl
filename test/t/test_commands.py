from .mpris import MprisPlayer, setup_buses, get_interfaces
from .playerctl import playerctl
from dbus_next.aio import session_bus

import asyncio
import pytest


@pytest.mark.asyncio
async def test_commands():
    [bus] = await setup_buses('commands')
    [interface] = get_interfaces(bus)

    commands = ('play', 'pause', 'play-pause', 'stop', 'next', 'previous')

    def get_called(cmd):
        return getattr(interface, f'{cmd.replace("-", "_")}_called')

    results = await asyncio.gather(*(playerctl(f'-p commands {cmd}')
                                     for cmd in commands))

    for i, result in enumerate(results):
        cmd = commands[i]
        assert get_called(cmd), f'{cmd} was not called'
