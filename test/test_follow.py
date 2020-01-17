from .mpris import setup_buses, get_interfaces
from .playerctl import PlayerctlCli
from dbus_next import Variant
import pytest
import asyncio


async def ping(bus):
    await bus.introspect('org.freedesktop.DBus', '/org/freedesktop/DBus')


class MprisWrapper:
    def __init__(self, bus):
        self.bus = bus
        self.counter = 0
        self.iface = get_interfaces(bus)[0]
        self.iface.playback_status = 'Playing'

    async def set_metadata(self, artist, title):
        self.counter += 1
        self.iface.metadata = {
            'xesam:title': Variant('s', title),
            'xesam:artist': Variant('as', [artist]),
            'mpris:trackid': Variant('o', '/0'),
        }

        self.iface.emit_properties_changed({
            'Metadata':
            self.iface.metadata,
            'PlaybackStatus':
            self.iface.playback_status
        })
        await ping(self.bus)

    async def clear_metadata(self):
        self.counter += 1
        self.iface.metadata = {
            'mpris:trackid': Variant('o', '/0'),
        }
        self.iface.emit_properties_changed({
            'Metadata':
            self.iface.metadata,
            'PlaybackStatus':
            self.iface.playback_status
        })
        await ping(self.bus)


class ProcWrapper:
    def __init__(self, proc):
        self.queue = asyncio.Queue()
        self.proc = proc

        async def reader(stream):
            while True:
                line = await stream.readline()
                print(line)
                if not line:
                    break
                line = line.decode().strip()
                await self.queue.put(line)

        async def printer(stream):
            while True:
                line = await stream.readline()
                print(line)
                if not line:
                    break

        asyncio.get_event_loop().create_task(reader(proc.stdout))
        asyncio.get_event_loop().create_task(printer(proc.stderr))

    def running(self):
        return self.proc.returncode == None


@pytest.mark.skip
@pytest.mark.asyncio
async def test_follow(bus_address):
    playerctl = PlayerctlCli(bus_address, debug=True)
    pctl_cmd = '--all-players metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = ProcWrapper(await playerctl.start(pctl_cmd))

    player1 = 'test1'
    player2 = 'test2'
    [bus1, bus2] = await setup_buses(player1, player2, bus_address=bus_address)

    mpris1, mpris2 = [MprisWrapper(bus) for bus in [bus1, bus2]]

    await mpris1.set_metadata('artist', 'title')
    line = await proc.queue.get()
    assert proc.running()
    assert line == 'test1: artist - title'

    await mpris1.set_metadata('artist2', 'title2')
    assert proc.running()
    line = await proc.queue.get()
    assert line == 'test1: artist2 - title2'

    mpris1.clear_metadata()
    line = await proc.queue.get()
    assert line == ''

    await mpris1.set_metadata('artist3', 'title3')
    assert proc.running()
    line = await proc.queue.get()
    assert line == 'test1: artist3 - title3'

    mpris1.set_metadata('artist4', 'title4')
    await ping(bus1)
    assert proc.queue.qsize() == 1

    bus1.disconnect()
    bus2.disconnect()

    line = await proc.queue.get()
    assert line == ''


@pytest.mark.skip
@pytest.mark.asyncio
async def test_follow_selection(bus_address):
    return
    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = 'metadata --player test3,test2,test1 --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = ProcWrapper(await playerctl.start(pctl_cmd))

    player1 = 'test1'
    player2 = 'test2'
    player3 = 'test3'
    [bus1, bus2, bus3] = await setup_buses(player1,
                                           player2,
                                           player3,
                                           bus_address=bus_address)
    [mpris1, mpris2,
     mpris3] = [MprisWrapper(bus) for bus in [bus1, bus2, bus3]]
