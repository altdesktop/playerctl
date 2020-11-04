from .mpris import setup_mpris
from .playerctl import PlayerctlCli
import pytest


def selector(bus_address):
    playerctl = PlayerctlCli(bus_address)

    async def select(*players):
        assert players
        cmd = '-p ' + str.join(
            ',', players) + ' status --format "{{playerInstance}}"'
        result = await playerctl.run(cmd)
        assert result.returncode == 0, result.stderr
        return result.stdout

    async def select_many(*players):
        assert players
        cmd = '--all-players -p ' + str.join(
            ',', players) + ' status --format "{{playerInstance}}"'
        result = await playerctl.run(cmd)
        assert result.returncode == 0, result.stderr
        return tuple(result.stdout.split('\n'))

    return select, select_many


@pytest.mark.asyncio
async def test_selection(bus_address):
    s1 = 'selection1'
    s1i = 'selection1.i_123'
    s2 = 'selection2'
    s3 = 'selection3'
    m4 = 'selection4'
    m5 = 'selection5'
    m6 = 'selection6'
    s6i = 'selection6.i_2'
    any_player = '%any'

    mpris_players = await setup_mpris(s1,
                                      s1i,
                                      s2,
                                      s3,
                                      s6i,
                                      bus_address=bus_address)

    # TODO: test ignored players
    selections = {
        (s1, ): (s1, s1i),
        (s3, s1): (s3, s1, s1i),
        (s2, s1, s3): (s2, s1, s1i, s3),
        (s1, s2): (s1, s1i, s2),
        (m4, m5, s2, s3): (s2, s3),
        (m5, s1, m4, s3): (s1, s1i, s3),
        (s1, s1i): (s1, s1i),
        (s1i, s1): (s1i, s1),
        (m6, s1): (s6i, s1, s1i),
        (m4, m6, s3): (s6i, s3),
        (any_player, ):
        (s2, s3, s1i, s6i, s1),  # order undefined, but consistent
        (s1, any_player): (s1, s1i, s2, s3, s6i),  # s1 first
        (any_player, s1): (s2, s3, s6i, s1i, s1),  # s1 last
        (m6, any_player, s2): (s6i, s3, s1i, s1, s2),  # s6 first, s2 last
        (m6, s1, any_player, s2): (s6i, s1, s1i, s3, s2),
    }

    select, select_many = selector(bus_address)

    for selection, expected in selections.items():
        result = await select(*selection)
        assert result == expected[0]
        result = await select_many(*selection)
        assert result == expected

    for mpris in mpris_players:
        mpris.disconnect()
