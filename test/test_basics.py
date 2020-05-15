from .mpris import setup_mpris
from .playerctl import PlayerctlCli
import math

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
    mpris_players = await setup_mpris('basics1',
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

    for mpris in mpris_players:
        mpris.disconnect()


@pytest.mark.asyncio
async def test_system_list_players(bus_address):
    system_players = await setup_mpris('system', system=True)
    session_players = await setup_mpris('session1', bus_address=bus_address)
    playerctl = PlayerctlCli(bus_address, debug=False)
    result = await playerctl.run('-l')
    assert result.returncode == 0, result.stdout
    assert result.stdout.split() == ['session1', 'system']
    for mpris in system_players + session_players:
        mpris.disconnect()


@pytest.mark.asyncio
async def test_queries(bus_address):
    [mpris] = await setup_mpris('queries', bus_address=bus_address)
    mpris.position = 2500000

    playerctl = PlayerctlCli(bus_address)

    query = await playerctl.run('status')
    assert query.stdout == mpris.playback_status, query.stderr

    query = await playerctl.run('volume')
    assert float(query.stdout) == mpris.volume, query.stderr

    query = await playerctl.run('loop')
    assert query.stdout == mpris.loop_status, query.stderr

    query = await playerctl.run('position')
    assert math.fabs(float(query.stdout) * 1000000 -
                     mpris.position) < 100, query.stderr

    query = await playerctl.run('shuffle')
    assert query.stdout == ('On' if mpris.shuffle else 'Off'), query.stderr
