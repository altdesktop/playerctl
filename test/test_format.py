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
    TITLE = 'a title'
    ARTIST = 'an artist'
    mpris.metadata = {
        'xesam:title': Variant('s', TITLE),
        'xesam:artist': Variant('as', [ARTIST])
    }
    playerctl = PlayerctlCli(bus_address)

    cmd = await playerctl.run('metadata --format "{{artist}} - {{title}}"')
    assert cmd.stdout == f'{ARTIST} - {TITLE}'
