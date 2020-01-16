from .mpris import setup_buses
from .playerctl import PlayerctlCli
import pytest
import asyncio


def selector(bus_address):
    playerctl = PlayerctlCli(bus_address)

    async def select(*players):
        cmd = '-p ' + str.join(
            ',', players) + ' status --format "{{playerInstance}}"'
        result = await playerctl.run(cmd)
        assert result.returncode == 0, result.stderr
        return result.stdout

    return select


@pytest.mark.asyncio
async def test_selection(bus_address):
    selection1 = 'selection1'
    selection1_instance = 'selection1.instance123'
    selection2 = 'selection2'
    selection3 = 'selection3'
    selection_missing1 = 'selection4'
    selection_missing2 = 'selection5'
    selection6 = 'selection6'
    selection6_instance = 'selection6.instance2'
    buses = await setup_buses(selection1,
                              selection1_instance,
                              selection2,
                              selection3,
                              selection6_instance,
                              bus_address=bus_address)

    select = selector(bus_address)

    for selection in [selection1, selection2, selection3]:
        result = await select(selection)
        assert result == selection

    result = await select(selection3, selection1)
    assert result == selection3

    result = await select(selection2, selection1, selection3)
    assert result == selection2

    result = await select(selection1, selection2)
    assert result == selection1

    result = await select(selection_missing1, selection_missing2, selection2,
                          selection3)
    assert result == selection2

    result = await select(selection_missing2, selection1, selection_missing1,
                          selection3)
    assert result == selection1

    result = await select(selection1, selection1_instance)
    assert result == selection1

    result = await select(selection1_instance, selection1)
    assert result == selection1_instance

    result = await select(selection6, selection1)
    assert result == selection6_instance

    result = await select(selection_missing1, selection6, selection3)
    assert result == selection6_instance

    for bus in buses:
        bus.disconnect()
