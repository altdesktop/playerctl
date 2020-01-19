from .mpris import setup_buses, get_interfaces
from .playerctl import PlayerctlCli
from dbus_next import Variant
import pytest
import asyncio


class MprisWrapper:
    def __init__(self, bus):
        self.bus = bus
        self.counter = 0
        self.iface = get_interfaces(bus)[0]
        self.iface.playback_status = 'Playing'

    async def ping(self):
        await self.bus.introspect('org.freedesktop.DBus',
                                  '/org/freedesktop/DBus')

    async def set_metadata(self, artist, title):
        self.counter += 1
        self.iface.metadata = {
            'xesam:title': Variant('s', title),
            'xesam:artist': Variant('as', [artist]),
            'mpris:trackid': Variant('o', '/' + str(self.counter)),
        }

        self.iface.emit_properties_changed({
            'Metadata': self.iface.metadata,
            'PlaybackStatus': self.iface.playback_status,
            'CanPlay': True,
        })
        await self.ping()

    async def clear_metadata(self):
        self.counter += 1
        self.iface.metadata = {
            'mpris:trackid': Variant('o', '/' + str(self.counter)),
        }
        self.iface.emit_properties_changed({
            'Metadata': self.iface.metadata,
            'PlaybackStatus': self.iface.playback_status,
            'CanPlay': True,
        })
        await self.ping()


class ProcWrapper:
    def __init__(self, proc):
        self.queue = asyncio.Queue()
        self.proc = proc

        async def reader(stream):
            while True:
                line = await stream.readline()
                if not line:
                    break
                line = line.decode().strip()
                if 'playerctl-DEBUG:' in line:
                    print(line)
                else:
                    await self.queue.put(line)

        async def printer(stream):
            while True:
                line = await stream.readline()
                print(line)
                if not line:
                    break

        asyncio.get_event_loop().create_task(reader(proc.stdout))
        # asyncio.get_event_loop().create_task(printer(proc.stderr))

    def running(self):
        return self.proc.returncode == None


@pytest.mark.asyncio
async def test_follow(bus_address):
    player1 = 'test1'
    [bus1] = await setup_buses(player1, bus_address=bus_address)

    mpris1 = MprisWrapper(bus1)

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = 'metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = ProcWrapper(await playerctl.start(pctl_cmd))

    await mpris1.set_metadata('artist', 'title')
    line = await proc.queue.get()
    assert line == 'test1: artist - title'

    await mpris1.set_metadata('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test1: artist2 - title2'

    await mpris1.clear_metadata()
    line = await proc.queue.get()
    assert line == 'test1:  -'

    await mpris1.set_metadata('artist3', 'title3')
    line = await proc.queue.get()
    assert line == 'test1: artist3 - title3'

    bus1.disconnect()

    line = await proc.queue.get()
    assert line == ''


@pytest.mark.asyncio
async def test_follow_selection(bus_address):
    player1 = 'test1'
    player2 = 'test2'
    player3 = 'test3'
    player4 = 'test4'
    [bus1, bus2, bus3, bus4] = await setup_buses(player1,
                                                 player2,
                                                 player3,
                                                 player4,
                                                 bus_address=bus_address)
    [mpris1, mpris2, mpris3,
     mpris4] = [MprisWrapper(bus) for bus in [bus1, bus2, bus3, bus4]]

    await mpris1.set_metadata('artist', 'title')

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = '--player test3,test2,test1 metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = ProcWrapper(await playerctl.start(pctl_cmd))

    line = await proc.queue.get()
    assert line == 'test1: artist - title'

    # player4 is ignored
    await mpris4.set_metadata('artist', 'title')
    assert proc.queue.empty()

    # setting metadata the same twice doesn't print
    await mpris1.set_metadata('artist', 'title')
    assert proc.queue.empty()

    await mpris2.set_metadata('artist', 'title')
    line = await proc.queue.get()
    assert line == 'test2: artist - title'

    # player2 takes precedence
    await mpris1.set_metadata('artist2', 'title2')
    assert proc.queue.empty()

    await mpris3.set_metadata('artist', 'title')
    line = await proc.queue.get()
    assert line == 'test3: artist - title'

    # player 3 takes precedence
    await mpris2.set_metadata('artist2', 'title2')
    assert proc.queue.empty()

    # when bus3 disconnects, it should show the next one
    bus3.disconnect()

    await mpris2.ping()
    line = await proc.queue.get()
    assert line == 'test2: artist2 - title2'

    # same for bus2
    bus2.disconnect()
    await mpris1.ping()
    line = await proc.queue.get()
    assert line == 'test1: artist2 - title2'

    bus1.disconnect()
    line = await proc.queue.get()
    assert line == ''

    bus4.disconnect()


@pytest.mark.asyncio
async def test_follow_selection_any(bus_address):
    player1 = 'test1'
    player2 = 'test2'
    player3 = 'test3'
    player4 = 'test4'
    [bus1, bus2, bus3, bus4] = await setup_buses(player1,
                                                 player2,
                                                 player3,
                                                 player4,
                                                 bus_address=bus_address)

    [mpris1, mpris2, mpris3,
     mpris4] = [MprisWrapper(bus) for bus in [bus1, bus2, bus3, bus4]]

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = '--player test3,%any,test1 metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = ProcWrapper(await playerctl.start(pctl_cmd))

    # test3 takes first precedence
    await mpris3.set_metadata('artist', 'title')
    line = await proc.queue.get()
    assert line == 'test3: artist - title', proc.queue

    await mpris2.set_metadata('artist', 'title')
    assert proc.queue.empty()

    await mpris1.set_metadata('artist', 'title')
    assert proc.queue.empty()

    bus3.disconnect()
    line = await proc.queue.get()
    assert line == 'test2: artist - title'

    bus2.disconnect()
    line = await proc.queue.get()
    assert line == 'test1: artist - title'

    bus1.disconnect()
    line = await proc.queue.get()
    assert line == ''

    bus4.disconnect()


@pytest.mark.asyncio
async def test_follow_all_players(bus_address):
    player1 = 'test1'
    player2 = 'test2'
    player3 = 'test3'
    player4 = 'test4'
    [bus1, bus2, bus3, bus4] = await setup_buses(player1,
                                                 player2,
                                                 player3,
                                                 player4,
                                                 bus_address=bus_address)
    [mpris1, mpris2, mpris3,
     mpris4] = [MprisWrapper(bus) for bus in [bus1, bus2, bus3, bus4]]

    await asyncio.gather(*[
        mpris.set_metadata('artist', 'title')
        for mpris in [mpris1, mpris2, mpris3, mpris4]
    ])

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = '--all-players --player test3,test2,test1 metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = ProcWrapper(await playerctl.start(pctl_cmd))

    # player4 is ignored
    await mpris4.set_metadata('artist', 'title')
    assert proc.queue.empty()

    # no precedence, just whoever changes metadata last
    await mpris1.set_metadata('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test1: artist2 - title2'

    await mpris2.set_metadata('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test2: artist2 - title2'

    await mpris3.set_metadata('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test3: artist2 - title2'

    await mpris2.set_metadata('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test2: artist2 - title2'

    await mpris1.set_metadata('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test1: artist2 - title2'

    bus1.disconnect()
    await mpris4.ping()

    line = await proc.queue.get()
    assert line == 'test2: artist2 - title2'

    bus2.disconnect()
    await mpris4.ping()

    line = await proc.queue.get()
    assert line == 'test3: artist2 - title2'

    bus3.disconnect()
    await mpris4.ping()

    line = await proc.queue.get()
    assert line == ''

    bus4.disconnect()
