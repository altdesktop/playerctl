from dbus_next import RequestNameReply, Variant
from dbus_next.aio import MessageBus
from .mpris import MprisPlayer
from .playerctl import PlayerctlCli

import pytest
import asyncio

@pytest.mark.asyncio
async def test_format(bus_address):
    bus = await MessageBus(bus_address=bus_address).connect()
    reply = await bus.request_name(f'org.mpris.MediaPlayer2.format_test')
    assert reply == RequestNameReply.PRIMARY_OWNER
    mpris = MprisPlayer()
    bus.export('/org/mpris/MediaPlayer2', mpris)
    TITLE = 'A Title'
    ARTIST = 'An Artist'
    mpris.metadata = {
        'xesam:title': Variant('s', TITLE),
        'xesam:artist': Variant('as', [ARTIST]),
        'xesam:escapeme': Variant('s', '<hi>'),
        'mpris:length': Variant('x', 100000)
    }

    playerctl = PlayerctlCli(bus_address)

    cmd = await playerctl.run('metadata --format "{{artist}} - {{title}}"')
    assert cmd.stdout == f'{ARTIST} - {TITLE}'

    cmd = await playerctl.run('metadata --format "{{markup_escape(xesam:escapeme)}}"')
    assert cmd.stdout == '&lt;hi&gt;'

    cmd = await playerctl.run('metadata --format "{{lc(artist)}}"')
    assert cmd.stdout == ARTIST.lower()

    cmd = await playerctl.run('metadata --format "{{uc(title)}}"')
    assert cmd.stdout == TITLE.upper()
