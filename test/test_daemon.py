import pytest

import os
from .mpris import setup_mpris
from .playerctl import PlayerctlCli

import asyncio
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
    cmd = await run('play')

    for result in results:
        assert cmd.returncode == 1
        assert 'No player could handle this command' in cmd.stderr.splitlines()

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

    mpris3.disconnect()
    cmd = await run('play')
    assert cmd.returncode == 0, cmd.stderr
    assert mpris1.play_called
    mpris1.reset()

    mpris1.disconnect()
    mpris2.disconnect()

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

    mpris1.disconnect()
    line = await proc.queue.get()
    assert line == 'playerctld: artist3 - title3', proc.queue

    for mpris in (mpris2, mpris3):
        mpris.disconnect()

    playerctld_proc.terminate()
    proc.proc.terminate()
    await proc.proc.wait()
    await playerctld_proc.wait()


async def playerctld_shift(bus_address):
    env = os.environ.copy()
    env['DBUS_SESSION_BUS_ADDRESS'] = bus_address
    env['G_MESSAGES_DEBUG'] = 'playerctld_shift'
    shift = await asyncio.create_subprocess_shell(
        'playerctld shift',
        env=env,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT)
    return await shift.wait()


@pytest.mark.asyncio
async def test_daemon_shift_simple(bus_address):
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

    code = await playerctld_shift(bus_address)
    assert code == 0
    line = await proc.queue.get()
    assert line == 'playerctld: artist1 - title1', proc.queue

    code = await playerctld_shift(bus_address)
    assert code == 0
    line = await proc.queue.get()
    assert line == 'playerctld: artist2 - title2', proc.queue

    for mpris in (mpris1, mpris2):
        mpris.disconnect()

    playerctld_proc.terminate()
    proc.proc.terminate()
    await proc.proc.wait()
    await playerctld_proc.wait()


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

    mpris1.disconnect()
    code = await playerctld_shift(bus_address)
    assert code == 1

    playerctld_proc.terminate()
    await playerctld_proc.wait()
