from .mpris import setup_buses
from .playerctl import PlayerctlCli

import asyncio
import pytest


@pytest.mark.asyncio
async def test_basics():
    playerctl = PlayerctlCli()

    result = await playerctl.run('--help')
    assert result.returncode == 0, result.stderr
    assert result.stdout
    assert not result.stderr

    # with no players
    result = await playerctl.run('--list-all')
    assert result.returncode == 0, result.stderr
    assert not result.stdout
    assert result.stderr

    result = await playerctl.run('--version')
    assert result.returncode == 0, result.stderr
    assert result.stdout
    assert not result.stderr

    commands = ('play', 'pause', 'play-pause', 'stop', 'next', 'previous',
                'position', 'position 5', 'volume', 'volume 0.5', 'status',
                'metadata', 'loop', 'loop None', 'shuffle', 'shuffle On',
                'open https://google.com')

    results = await asyncio.gather(*(playerctl.run(cmd) for cmd in commands))

    for result in results:
        assert result.returncode == 1
        assert not result.stdout
        assert 'No players found' in result.stderr.split('\n')


@pytest.mark.asyncio
async def test_list_names(bus_address):
    [bus1, bus2, bus3] = await setup_buses('basics1',
                                           'basics2',
                                           'basics3',
                                           bus_address=bus_address)
    playerctl = PlayerctlCli(bus_address)

    result = await playerctl.run('--list-all')
    assert result.returncode == 0, result.stderr
    players = result.stdout.splitlines()
    assert 'basics1' in players
    assert 'basics2' in players
    assert 'basics3' in players

    for bus in [bus1, bus2, bus3]:
        bus.disconnect()
