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

    playerctl = PlayerctlCli(bus_address, debug=True)

    results = await asyncio.gather(*(playerctl.run(f'-p commands {cmd}')
                                     for cmd in commands + setters))

    for i, cmd in enumerate(commands):
        result = results[i]
        assert get_called(cmd), f'{cmd} was not called: {result.stderr}'

    assert mpris.shuffle
    assert mpris.volume == 0.8
    assert mpris.loop_status == 'Playlist'

    await playerctl.run('-p commands shuffle toggle')

    assert not mpris.shuffle
