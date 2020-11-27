import pytest

import os
from .mpris import setup_mpris
from .playerctl import PlayerctlCli
from dbus_next.aio import MessageBus
from dbus_next import Message, MessageType

import asyncio
from asyncio import Queue
from subprocess import run as run_process


async def start_playerctld(bus_address, debug=False):
    pkill = await asyncio.create_subprocess_shell('pkill playerctld')
    await pkill.wait()
    env = os.environ.copy()
    env['DBUS_SESSION_BUS_ADDRESS'] = bus_address
    env['G_MESSAGES_DEBUG'] = 'playerctl'
    proc = await asyncio.create_subprocess_shell(
        'playerctld',
        env=env,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT)

    async def printer(stream):
        while True:
            line = await stream.readline()
            print(line)
            if not line:
                break

    if debug:
        asyncio.get_event_loop().create_task(printer(proc.stdout))

    return proc


async def get_playerctld(bus):
    path = '/com/github/altdesktop/playerctld'
    interface = 'com.github.altdesktop.playerctld'
    introspection = await bus.introspect('org.mpris.MediaPlayer2.playerctld',
                                         path)
    obj = bus.get_proxy_object(interface, path, introspection)
    return obj.get_interface('org.freedesktop.DBus.Properties')


@pytest.mark.asyncio
async def test_daemon_commands(bus_address):
    playerctl = PlayerctlCli(bus_address)

    async def run(cmd):
        return await playerctl.run('-p playerctld ' + cmd)

    # with no other players running, these should error because there's no
    # active player (not no players found). This tests activation and property
    # errors as well.
    results = await asyncio.gather(*(run(cmd)
                                     for cmd in ('play', 'pause', 'play-pause',
                                                 'stop', 'next', 'previous',
                                                 'position', 'volume',
                                                 'status', 'metadata', 'loop',
                                                 'shuffle')))
    for result in results:
        assert result.returncode == 1
        assert 'No player could handle this command' in result.stderr.splitlines(
        )

    # restart playerctld so we can manage the process and see debug info
    playerctld_proc = await start_playerctld(bus_address)

    [mpris1, mpris2, mpris3] = await setup_mpris('daemon1',
                                                 'daemon2',
                                                 'daemon3',
                                                 bus_address=bus_address)
    await mpris2.set_artist_title('artist', 'title')
    cmd = await run('play')
    assert cmd.returncode == 0, cmd.stdout
    assert mpris2.play_called, cmd.stdout
    mpris2.reset()

    await mpris1.set_artist_title('artist', 'title')
    cmd = await run('play')
    assert cmd.returncode == 0, cmd.stderr
    assert mpris1.play_called
    mpris1.reset()

    await mpris3.set_artist_title('artist', 'title')
    cmd = await run('play')
    assert cmd.returncode == 0, cmd.stderr
    assert mpris3.play_called
    mpris3.reset()

    await mpris3.disconnect()
    cmd = await run('play')
    assert cmd.returncode == 0, cmd.stderr
    assert mpris1.play_called
    mpris1.reset()

    await asyncio.gather(mpris1.disconnect(), mpris2.disconnect())

    playerctld_proc.terminate()
    await playerctld_proc.wait()


@pytest.mark.asyncio
async def test_daemon_follow(bus_address):
    playerctld_proc = await start_playerctld(bus_address)

    [mpris1, mpris2] = await setup_mpris('player1',
                                         'player2',
                                         bus_address=bus_address)
    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = '--player playerctld metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = await playerctl.start(pctl_cmd)

    await mpris1.set_artist_title('artist1', 'title1')
    line = await proc.queue.get()
    assert line == 'playerctld: artist1 - title1', proc.queue

    await mpris2.set_artist_title('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'playerctld: artist2 - title2', proc.queue

    [mpris3] = await setup_mpris('player3', bus_address=bus_address)
    await mpris3.set_artist_title('artist3', 'title3')
    line = await proc.queue.get()
    # I don't really care if this is blank, but happens by test setup
    assert line == ''
    line = await proc.queue.get()
    assert line == 'playerctld: artist3 - title3', proc.queue

    await mpris1.set_artist_title('artist4', 'title4')
    line = await proc.queue.get()
    assert line == 'playerctld: artist4 - title4', proc.queue

    await mpris1.set_artist_title('artist5', 'title5')
    line = await proc.queue.get()
    assert line == 'playerctld: artist5 - title5', proc.queue

    await mpris1.disconnect()
    line = await proc.queue.get()
    assert line == 'playerctld: artist3 - title3', proc.queue

    await asyncio.gather(mpris2.disconnect(), mpris3.disconnect())

    playerctld_proc.terminate()
    proc.proc.terminate()
    await proc.proc.wait()
    await playerctld_proc.wait()


async def playerctld_shift(bus_address, reverse = False):
    env = os.environ.copy()
    env['DBUS_SESSION_BUS_ADDRESS'] = bus_address
    env['G_MESSAGES_DEBUG'] = 'playerctl'
    cmd = 'playerctld unshift' if reverse else 'playerctld shift'
    shift = await asyncio.create_subprocess_shell(
        cmd,
        env=env,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT)
    return await shift.wait()


@pytest.mark.asyncio
async def test_daemon_shift_simple(bus_address):
    playerctld_proc = await start_playerctld(bus_address)

    mprises = await setup_mpris('player1', 'player2', 'player3', bus_address=bus_address)
    [mpris1, mpris2, mpris3] = mprises

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = '--player playerctld metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = await playerctl.start(pctl_cmd)

    await mpris1.set_artist_title('artist1', 'title1')
    line = await proc.queue.get()
    assert line == 'playerctld: artist1 - title1', proc.queue

    await mpris2.set_artist_title('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'playerctld: artist2 - title2', proc.queue

    await mpris3.set_artist_title('artist3', 'title3')
    line = await proc.queue.get()
    assert line == 'playerctld: artist3 - title3', proc.queue

    code = await playerctld_shift(bus_address)
    assert code == 0
    line = await proc.queue.get()
    assert line == 'playerctld: artist2 - title2', proc.queue

    code = await playerctld_shift(bus_address)
    assert code == 0
    line = await proc.queue.get()
    assert line == 'playerctld: artist1 - title1', proc.queue

    code = await playerctld_shift(bus_address, reverse=True)
    assert code == 0
    line = await proc.queue.get()
    assert line == 'playerctld: artist2 - title2', proc.queue

    code = await playerctld_shift(bus_address, reverse=True)
    assert code == 0
    line = await proc.queue.get()
    assert line == 'playerctld: artist3 - title3', proc.queue

    playerctld_proc.terminate()
    proc.proc.terminate()
    await asyncio.gather(mpris1.disconnect(), mpris2.disconnect(),
                         playerctld_proc.wait(), proc.proc.wait())


@pytest.mark.asyncio
async def test_daemon_shift_no_player(bus_address):
    playerctld_proc = await start_playerctld(bus_address)

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = '--player playerctld metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = await playerctl.start(pctl_cmd)

    code = await playerctld_shift(bus_address)
    assert code == 1

    [mpris1] = await setup_mpris('player1', bus_address=bus_address)
    code = await playerctld_shift(bus_address)
    assert code == 0

    await mpris1.disconnect()
    code = await playerctld_shift(bus_address)
    assert code == 1

    code = await playerctld_shift(bus_address, reverse=True)
    assert code == 1

    [mpris1] = await setup_mpris('player1', bus_address=bus_address)
    code = await playerctld_shift(bus_address, reverse=True)
    assert code == 0

    await mpris1.disconnect()
    code = await playerctld_shift(bus_address, reverse=True)
    assert code == 1

    playerctld_proc.terminate()
    await playerctld_proc.wait()


@pytest.mark.asyncio
async def test_active_player_change(bus_address):
    queue = Queue()
    playerctld_proc = await start_playerctld(bus_address)

    bus = await MessageBus(bus_address=bus_address).connect()

    reply = await bus.call(
        Message(destination='org.freedesktop.DBus',
                interface='org.freedesktop.DBus',
                path='/org/freedesktop/DBus',
                member='AddMatch',
                signature='s',
                body=["sender='org.mpris.MediaPlayer2.playerctld'"]))

    assert reply.message_type == MessageType.METHOD_RETURN, reply.body

    def message_handler(message):
        if message.member == 'PropertiesChanged' and message.body[
                0] == 'com.github.altdesktop.playerctld' and 'PlayerNames' in message.body[
                    1]:
            queue.put_nowait(message.body[1]['PlayerNames'].value)

    def player_list(*args):
        return [f'org.mpris.MediaPlayer2.{name}' for name in args]

    bus.add_message_handler(message_handler)

    [mpris1] = await setup_mpris('player1', bus_address=bus_address)

    assert player_list('player1') == await queue.get()

    [mpris2] = await setup_mpris('player2', bus_address=bus_address)

    assert player_list('player2', 'player1') == await queue.get()

    # changing artist/title should bump the player up
    await mpris1.set_artist_title('artist1', 'title1', '/1')

    assert player_list('player1', 'player2') == await queue.get()

    # if properties are not actually different, it shouldn't update
    await mpris2.set_artist_title('artist2', 'title2', '/2')
    assert player_list('player2', 'player1') == await queue.get()

    await mpris1.set_artist_title('artist1', 'title1', '/1')
    await mpris1.ping()
    assert queue.empty()

    bus.disconnect()
    await asyncio.gather(mpris1.disconnect(), mpris2.disconnect(),
                         bus.wait_for_disconnect())

    playerctld_proc.terminate()
    await playerctld_proc.wait()
