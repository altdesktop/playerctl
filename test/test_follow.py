from .mpris import setup_mpris
from .playerctl import PlayerctlCli
import pytest
import asyncio


@pytest.mark.asyncio
async def test_follow(bus_address):
    player1 = 'test1'
    [mpris1] = await setup_mpris(player1, bus_address=bus_address)

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = 'metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = await playerctl.start(pctl_cmd)

    await mpris1.set_artist_title('artist', 'title')
    line = await proc.queue.get()
    assert line == 'test1: artist - title'

    await mpris1.set_artist_title('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test1: artist2 - title2'

    await mpris1.clear_metadata()
    line = await proc.queue.get()
    assert line == 'test1:  -'

    await mpris1.set_artist_title('artist3', 'title3')
    line = await proc.queue.get()
    assert line == 'test1: artist3 - title3'

    mpris1.disconnect()

    line = await proc.queue.get()
    assert line == ''


@pytest.mark.asyncio
async def test_follow_selection(bus_address):
    player1 = 'test1'
    player2 = 'test2'
    player3 = 'test3'
    player4 = 'test4'
    [mpris1, mpris2, mpris3,
     mpris4] = await setup_mpris(player1,
                                 player2,
                                 player3,
                                 player4,
                                 bus_address=bus_address)

    await mpris1.set_artist_title('artist', 'title')

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = '--player test3,test2,test1 metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = await playerctl.start(pctl_cmd)

    line = await proc.queue.get()
    assert line == 'test1: artist - title'

    # player4 is ignored
    await mpris4.set_artist_title('artist', 'title')
    assert proc.queue.empty()

    # setting metadata the same twice doesn't print
    await mpris1.set_artist_title('artist', 'title')
    assert proc.queue.empty()

    await mpris2.set_artist_title('artist', 'title')
    line = await proc.queue.get()
    assert line == 'test2: artist - title'

    # player2 takes precedence
    await mpris1.set_artist_title('artist2', 'title2')
    assert proc.queue.empty()

    await mpris3.set_artist_title('artist', 'title')
    line = await proc.queue.get()
    assert line == 'test3: artist - title'

    # player 3 takes precedence
    await mpris2.set_artist_title('artist2', 'title2')
    assert proc.queue.empty()

    # when bus3 disconnects, it should show the next one
    mpris3.disconnect()

    await mpris2.ping()
    line = await proc.queue.get()
    assert line == 'test2: artist2 - title2'

    # same for bus2
    mpris2.disconnect()
    await mpris1.ping()
    line = await proc.queue.get()
    assert line == 'test1: artist2 - title2'

    mpris1.disconnect()
    line = await proc.queue.get()
    assert line == ''

    mpris4.disconnect()


@pytest.mark.asyncio
async def test_follow_selection_any(bus_address):
    player1 = 'test1'
    player2 = 'test2'
    player3 = 'test3'
    player4 = 'test4'
    [mpris1, mpris2, mpris3,
     mpris4] = await setup_mpris(player1,
                                 player2,
                                 player3,
                                 player4,
                                 bus_address=bus_address)

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = '--player test3,%any,test1 metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = await playerctl.start(pctl_cmd)

    # test3 takes first precedence
    await mpris3.set_artist_title('artist', 'title')
    line = await proc.queue.get()
    assert line == 'test3: artist - title', proc.queue

    await mpris2.set_artist_title('artist', 'title')
    assert proc.queue.empty()

    await mpris1.set_artist_title('artist', 'title')
    assert proc.queue.empty()

    mpris3.disconnect()
    line = await proc.queue.get()
    assert line == 'test2: artist - title'

    mpris2.disconnect()
    line = await proc.queue.get()
    assert line == 'test1: artist - title'

    mpris1.disconnect()
    line = await proc.queue.get()
    assert line == ''

    mpris4.disconnect()


@pytest.mark.asyncio
async def test_follow_all_players(bus_address):
    player1 = 'test1'
    player2 = 'test2'
    player3 = 'test3'
    player4 = 'test4'
    [mpris1, mpris2, mpris3,
     mpris4] = await setup_mpris(player1,
                                 player2,
                                 player3,
                                 player4,
                                 bus_address=bus_address)

    await asyncio.gather(*[
        mpris.set_artist_title('artist', 'title')
        for mpris in [mpris1, mpris2, mpris3, mpris4]
    ])

    playerctl = PlayerctlCli(bus_address)
    pctl_cmd = '--all-players --player test3,test2,test1 metadata --format "{{playerInstance}}: {{artist}} - {{title}}" --follow'
    proc = await playerctl.start(pctl_cmd)

    # player4 is ignored
    await mpris4.set_artist_title('artist', 'title')
    assert proc.queue.empty()

    # no precedence, just whoever changes metadata last
    await mpris1.set_artist_title('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test1: artist2 - title2'

    await mpris2.set_artist_title('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test2: artist2 - title2'

    await mpris3.set_artist_title('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test3: artist2 - title2'

    await mpris2.set_artist_title('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test2: artist2 - title2'

    await mpris1.set_artist_title('artist2', 'title2')
    line = await proc.queue.get()
    assert line == 'test1: artist2 - title2'

    mpris1.disconnect()
    await mpris4.ping()

    line = await proc.queue.get()
    assert line == 'test2: artist2 - title2'

    mpris2.disconnect()
    await mpris4.ping()

    line = await proc.queue.get()
    assert line == 'test3: artist2 - title2'

    mpris3.disconnect()
    await mpris4.ping()

    line = await proc.queue.get()
    assert line == ''

    mpris4.disconnect()
