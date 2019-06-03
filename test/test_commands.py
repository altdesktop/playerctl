from .mpris import setup_buses, get_interfaces
from .playerctl import PlayerctlCli

import asyncio
import pytest


@pytest.mark.asyncio
async def test_commands(bus_address):
    [bus] = await setup_buses('commands', bus_address=bus_address)
    [interface] = get_interfaces(bus)

    commands = ('play', 'pause', 'play-pause', 'stop', 'next', 'previous')

    def get_called(cmd):
        return getattr(interface, f'{cmd.replace("-", "_")}_called')

    playerctl = PlayerctlCli(bus_address)

    results = await asyncio.gather(*(playerctl.run(f'-p commands {cmd}')
                                     for cmd in commands))

    for i, result in enumerate(results):
        cmd = commands[i]
        assert get_called(cmd), f'{cmd} was not called: {result.stderr}'
