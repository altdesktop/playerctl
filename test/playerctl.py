import asyncio
import os


class CommandResult:
    def __init__(self, stdout, stderr, returncode):
        self.stdout = stdout.decode().strip()
        self.stderr = stderr.decode().strip()
        self.returncode = returncode


class PlayerctlCli:
    def __init__(self, bus_address=None, debug=False):
        self.bus_address = bus_address
        self.debug = debug

    async def start(self, cmd):
        env = os.environ.copy()
        shell_cmd = f'playerctl {cmd}'

        if self.bus_address:
            env['DBUS_SESSION_BUS_ADDRESS'] = self.bus_address
        if self.debug:
            env['G_MESSAGES_DEBUG'] = 'playerctl'

        return await asyncio.create_subprocess_shell(
            shell_cmd,
            env=env,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE)

    async def run(self, cmd):
        proc = await self.start(cmd)
        stdout, stderr = await proc.communicate()
        await proc.wait()
        return CommandResult(stdout, stderr, proc.returncode)
