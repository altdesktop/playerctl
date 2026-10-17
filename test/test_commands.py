from .mpris import setup_mpris
from .playerctl import PlayerctlCli

import asyncio
import pytest

# TODO: test sending a command to all players


@pytest.mark.asyncio
async def test_commands(bus_address):
    [mpris] = await setup_mpris('commands', bus_address=bus_address)

    mpris.shuffle = False
    mpris.volume = 1.0
    mpris.loop_status = 'Track'

    commands = ('play', 'pause', 'play-pause', 'stop', 'next', 'previous')
    setters = ('volume 0.8', 'loop playlist', 'shuffle on')

    def get_called(cmd):
        return getattr(mpris, f'{cmd.replace("-", "_")}_called')

    playerctl = PlayerctlCli(bus_address)

    results = await asyncio.gather(
        *(playerctl.run(f'-p commands {cmd}') for cmd in commands + setters)
    )

    for result in results:
        assert result.returncode == 0, result.stderr

    for i, cmd in enumerate(commands):
        result = results[i]
        assert get_called(cmd), f'{cmd} was not called: {result.stderr}'

    assert mpris.shuffle
    assert mpris.volume == 0.8
    assert mpris.loop_status == 'Playlist'

    await playerctl.run('-p commands shuffle toggle')

    assert not mpris.shuffle

    await mpris.disconnect()


@pytest.mark.asyncio
async def test_volume_set(bus_address):
    [mpris] = await setup_mpris('volume-set', bus_address=bus_address)
    playerctl = PlayerctlCli(bus_address)

    for arg, expected in (('0.3', 0.3), ('50%', 0.5), ('1.5%', 0.015)):
        result = await playerctl.run(f'-p volume-set volume {arg}')
        assert result.returncode == 0, result.stderr
        assert mpris.volume == pytest.approx(expected), arg

    await mpris.disconnect()


@pytest.mark.asyncio
async def test_volume_relative(bus_address):
    [mpris] = await setup_mpris('volume-rel', bus_address=bus_address)
    playerctl = PlayerctlCli(bus_address)

    cases = (('0.1+', 0.6), ('0.1-', 0.4), ('10%+', 0.6), ('10%-', 0.4))
    for arg, expected in cases:
        mpris.volume = 0.5
        result = await playerctl.run(f'-p volume-rel volume {arg}')
        assert result.returncode == 0, result.stderr
        assert mpris.volume == pytest.approx(expected), arg

    await mpris.disconnect()


@pytest.mark.asyncio
async def test_volume_invalid(bus_address):
    [mpris] = await setup_mpris('volume-bad', bus_address=bus_address)
    playerctl = PlayerctlCli(bus_address)

    for arg in ("''", '%', 'abc%', '50%%', '5%x', '+', '10%+-'):
        mpris.volume = 0.5
        result = await playerctl.run(f'-p volume-bad volume {arg}')
        assert result.returncode == 1, (arg, result.stderr)
        assert 'could not parse volume' in result.stderr, arg
        assert mpris.volume == 0.5, arg

    await mpris.disconnect()
