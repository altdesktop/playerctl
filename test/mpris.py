from dbus_next.service import ServiceInterface, dbus_property, method, signal
from dbus_next import PropertyAccess, RequestNameReply
from dbus_next.aio import MessageBus

import asyncio


async def setup_buses(*names):
    async def setup(name):
        bus = await MessageBus().connect()
        reply = await bus.request_name(f'org.mpris.MediaPlayer2.{name}')
        assert reply == RequestNameReply.PRIMARY_OWNER
        bus.export('/org/mpris/MediaPlayer2', MprisPlayer())
        return bus

    return await asyncio.gather(*(setup(name) for name in names))


def get_interfaces(bus):
    return bus._path_exports.get('/org/mpris/MediaPlayer2', [])


class MprisPlayer(ServiceInterface):
    def __init__(self):
        super().__init__('org.mpris.MediaPlayer2.Player')
        self.reset()

    def reset(self):
        # method calls
        self.next_called = False
        self.previous_called = False
        self.pause_called = False
        self.play_pause_called = False
        self.stop_called = False
        self.play_called = False
        self.seek_called_with = None
        self.set_position_called_with = None
        self.open_uri_called_with = None

        # properties
        self.playback_status = 'Stopped'
        self.loop_status = 'None'
        self.rate = 1.0
        self.shuffle = False
        self.metadata = {}
        self.volume = 1.0
        self.position = 0
        self.minimum_rate = 1.0
        self.maximum_rate = 1.0
        self.can_go_next = True
        self.can_go_previous = True
        self.can_play = True
        self.can_pause = True
        self.can_seek = True
        self.can_control = True

        # signals
        self.seeked_value = 0

    @method()
    def Next(self):
        self.next_called = True

    @method()
    def Previous(self):
        self.previous_called = True

    @method()
    def Pause(self):
        self.pause_called = True

    @method()
    def PlayPause(self):
        self.play_pause_called = True

    @method()
    def Stop(self):
        self.stop_called = True

    @method()
    def Play(self):
        self.play_called = True

    @method()
    def Seek(self, offset: 'x'):
        self.seek_called_with = offset

    @method()
    def SetPosition(self, track_id: 'o', position: 'x'):
        self.set_position_called_with = (track_id, position)

    @method()
    def OpenUri(self, uri: 's'):
        self.open_uri_called_with = uri

    @signal()
    def Seeked(self) -> 'x':
        return self.seeked_value

    @dbus_property(access=PropertyAccess.READ)
    def PlaybackStatus(self) -> 's':
        return self.playback_status

    @dbus_property()
    def LoopStatus(self) -> 's':
        return self.loop_status

    @LoopStatus.setter
    def LoopStatus(self, status: 's'):
        self.loop_status = status

    @dbus_property()
    def Rate(self) -> 'd':
        return self.rate

    @Rate.setter
    def Rate(self, rate: 'd'):
        self.rate = rate

    @dbus_property()
    def Shuffle(self) -> 'b':
        return self.shuffle

    @Shuffle.setter
    def Shuffle(self, shuffle: 'b'):
        self.shuffle = shuffle

    @dbus_property(access=PropertyAccess.READ)
    def Metadata(self) -> 'a{sv}':
        return self.metadata

    @dbus_property()
    def Volume(self) -> 'd':
        return self.volume

    @Volume.setter
    def Volume(self, volume: 'd'):
        self.volume = volume

    @dbus_property(access=PropertyAccess.READ)
    def Position(self) -> 'x':
        return self.position

    @dbus_property(access=PropertyAccess.READ)
    def MinimumRate(self) -> 'd':
        return self.minimum_rate

    @dbus_property(access=PropertyAccess.READ)
    def MaximumRate(self) -> 'd':
        return self.maximum_rate

    @dbus_property(access=PropertyAccess.READ)
    def CanGoNext(self) -> 'b':
        return self.can_go_next

    @dbus_property(access=PropertyAccess.READ)
    def CanGoPrevious(self) -> 'b':
        return self.can_go_previous

    @dbus_property(access=PropertyAccess.READ)
    def CanPlay(self) -> 'b':
        return self.can_play

    @dbus_property(access=PropertyAccess.READ)
    def CanPause(self) -> 'b':
        return self.can_pause

    @dbus_property(access=PropertyAccess.READ)
    def CanSeek(self) -> 'b':
        return self.can_seek

    @dbus_property(access=PropertyAccess.READ)
    def CanControl(self) -> 'b':
        return self.can_control
