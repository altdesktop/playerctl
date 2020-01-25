from .mpris import setup_mpris
from .playerctl import PlayerctlCli

import asyncio
import pytest

# TODO: test sending a command to all players


@pytest.mark.asyncio
async def test_commands(bus_address):
    [mpris] = await setup_mpris('commands', bus_address=bus_address)

    commands = ('play', 'pause', 'play-pause', 'stop', 'next', 'previous')

    def get_called(cmd):
        return getattr(mpris, f'{cmd.replace("-", "_")}_called')

    playerctl = PlayerctlCli(bus_address)

    results = await asyncio.gather(*(playerctl.run(f'-p commands {cmd}')
                                     for cmd in commands))

    for i, result in enumerate(results):
        cmd = commands[i]
        assert get_called(cmd), f'{cmd} was not called: {result.stderr}'
