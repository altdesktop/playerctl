from .mpris import setup_buses
from .playerctl import playerctl

import asyncio
import pytest


@pytest.mark.asyncio
async def test_basics():
    result = await playerctl('--help')
    assert result.ret == 0, result.stderr
    assert result.stdout
    assert not result.stderr

    # with no players
    result = await playerctl('--list-all')
    assert result.ret == 0, result.stderr
    assert not result.stdout
    assert result.stderr

    result = await playerctl('--version')
    assert result.ret == 0, result.stderr
    assert result.stdout
    assert not result.stderr

    commands = ('play', 'pause', 'play-pause', 'stop', 'next', 'previous',
                'position', 'position 5', 'volume', 'volume 0.5', 'status',
                'metadata', 'loop', 'loop None', 'shuffle', 'shuffle On',
                'open https://google.com')

    results = await asyncio.gather(*(playerctl(cmd) for cmd in commands))

    for result in results:
        assert result.ret == 1
        assert not result.stdout
        assert result.stderr == 'No players found'


@pytest.mark.asyncio
async def test_list_names():
    [bus1, bus2, bus3] = await setup_buses('basics1', 'basics2', 'basics3')

    result = await playerctl('--list-all')
    assert result.ret == 0, result.stderr
    players = result.stdout.splitlines()
    assert 'basics1' in players
    assert 'basics2' in players
    assert 'basics3' in players

    for bus in [bus1, bus2, bus3]:
        bus.disconnect()
