from dbus_next import Variant
from .mpris import setup_mpris
from .playerctl import PlayerctlCli

import pytest
import asyncio

# TODO: test missing function does not segv


@pytest.mark.asyncio
async def test_emoji(bus_address):
    [mpris] = await setup_mpris('emoji-format-test', bus_address=bus_address)
    mpris.metadata = {'mpris:length': Variant('x', 100000)}
    await mpris.ping()
    playerctl = PlayerctlCli(bus_address)

    status_emoji_cmd = 'metadata --format \'{{emoji(status)}}\''

    mpris.playback_status = 'Playing'
    cmd = await playerctl.run(status_emoji_cmd)
    assert cmd.stdout == '▶️', cmd.stderr

    mpris.playback_status = 'Paused'
    cmd = await playerctl.run(status_emoji_cmd)
    assert cmd.stdout == '⏸️', cmd.stderr

    mpris.playback_status = 'Stopped'
    cmd = await playerctl.run(status_emoji_cmd)
    assert cmd.stdout == '⏹️', cmd.stderr

    volume_emoji_cmd = 'metadata --format \'{{emoji(volume)}}\''
    mpris.volume = 0.0
    cmd = await playerctl.run(volume_emoji_cmd)
    assert cmd.stdout == '🔈', cmd.stderr

    mpris.volume = 0.5
    cmd = await playerctl.run(volume_emoji_cmd)
    assert cmd.stdout == '🔉', cmd.stderr

    mpris.volume = 1.0
    cmd = await playerctl.run(volume_emoji_cmd)
    assert cmd.stdout == '🔊', cmd.stderr

    cmd = await playerctl.run('metadata --format \'{{emoji("hi")}}\'')
    assert cmd.returncode == 1, cmd.stderr

    cmd = await playerctl.run('metadata --format \'{{emoji(status, volume)}}\''
                              )
    assert cmd.returncode == 1, cmd.stderr


class MetadataTest:
    def __init__(self, playerctl):
        self.tests = []
        self.playerctl = playerctl

    def add(self, fmt, expected, ret=0):
        fmt = fmt.replace("'", r"\'")
        self.tests.append((f"metadata --format '{fmt}'", expected, ret))

    async def run(self):
        coros = []
        for fmt, _, _ in self.tests:
            coros.append(self.playerctl.run(fmt))

        results = await asyncio.gather(*coros)

        for i, cmd in enumerate(results):
            fmt, expected, ret = self.tests[i]
            assert cmd.returncode == ret, cmd.stderr
            if ret == 0:
                assert cmd.stdout == expected, cmd.stderr


@pytest.mark.asyncio
async def test_format(bus_address):
    title = 'A Title'
    artist = 'An Artist'
    album = 'An Album'
    player_name = 'format-test'
    player_instance = f'{player_name}.instance123'

    [mpris] = await setup_mpris(player_instance, bus_address=bus_address)
    mpris.metadata = {
        'xesam:title': Variant('s', title),
        'xesam:artist': Variant('as', [artist]),
        'xesam:escapeme': Variant('s', '<hi>'),
        'xesam:album': Variant('s', album),
        'mpris:length': Variant('x', 100000)
    }
    mpris.volume = 2.0

    playerctl = PlayerctlCli(bus_address)

    test = MetadataTest(playerctl)

    test.add('{{artist}} - {{title}}', f'{artist} - {title}')
    test.add("{{markup_escape(xesam:escapeme)}}", "&lt;hi&gt;")
    test.add("{{lc(artist)}}", artist.lower())
    test.add("{{uc(title)}}", title.upper())
    test.add("{{uc(lc(title))}}", title.upper())
    test.add('{{uc("Hi")}}', "HI")
    test.add("{{mpris:length}}", "100000")
    test.add(
        '@{{ uc( "hi" ) }} - {{uc( lc( "HO"  ) ) }} . {{lc( uc(  title ) )   }}@',
        f'@HI - HO . {title.lower()}@')
    test.add("{{default(xesam:missing, artist)}}", artist)
    test.add("{{default(title, artist)}}", title)
    test.add('{{default("", "ok")}}', 'ok')
    test.add('{{default("ok", "not")}}', 'ok')
    test.add(' {{lc(album)}} ', album.lower())
    test.add('{{playerName}} - {{playerInstance}}',
             f'{player_name} - {player_instance}')

    await test.run()

    # numbers
    math = [
        '10',
        '-10 + 20',
        '10 + 10',
        '10 * 10',
        '10 / 10',
        '10 + 10 * 10 + 10',
        '10 + 10 * -10 + 10',
        '10 + 10 * -10 + -10',
        '-10 * 10 + 10',
        '-10 * -10 * -1 + -10',
        '-10 * 10 + -10 * -10 + 20 / 10 * -20 + -10',
        '8+-+--++-4',
        '2 - 10 * 1 + 1',
        '2 / -2 + 2 * 2 * -2 - 2 - 2 * -2',
        '2 * (2 + 2)',
        '10 * (10 + 12) - 4',
        '-(10)',
        '-(10 + 12 * -2)',
        '14 - (10 * 2 + 5) * -6',
        '(14 - 2 * 3) * (14 * -2 - 6) + -(4 - 2) * 5',
    ]

    # variables
    math += [
        'volume',
        'volume + 10',
        '-volume',
        '-volume * -1',
        '-volume + volume',
        'volume * volume',
        'volume * -volume',
        'volume + volume * -volume * volume + -volume',
        'volume / -volume + volume * volume * -volume - volume - volume * -volume',
        '-(volume + 3) * 5 * (volume + 2)',
    ]

    # functions
    math += [
        'default(5+5, None)',
        '-default(5 + 5, None)',
        '(-default(5 - 5, None) + 2) * 8',
        '2 + (5 * 4 + 3 * -default(5, default(6 * (3 + 4 * (6 + 2)) / 2, None)) + -56)',
    ]

    def default_shim(arg1, arg2):
        if arg1 is None:
            return arg2
        return arg1

    async def math_test(math):
        cmd = await playerctl.run("metadata --format '{{" + math + "}}'")
        assert cmd.returncode == 0, cmd.stderr
        assert float(cmd.stdout) == eval(math, {
            'volume': mpris.volume,
            'default': default_shim
        }), math

    await asyncio.gather(*[math_test(m) for m in math])

    mpris.disconnect()
