import asyncio


class CommandResult:
    def __init__(self, stdout, stderr, ret):
        self.stdout = stdout.decode().strip()
        self.stderr = stderr.decode().strip()
        self.ret = ret


async def playerctl(cmd, bus_address=None):
    shell_cmd = f'playerctl {cmd}'

    if bus_address:
        shell_cmd = f'DBUS_SESSION_BUS_ADDRESS={bus_address} {shell_cmd}'

    proc = await asyncio.create_subprocess_shell(
        shell_cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE)
    stdout, stderr = await proc.communicate()
    await proc.wait()
    return CommandResult(stdout, stderr, proc.returncode)
