import asyncio
import os


class CommandResult:
    def __init__(self, stdout, stderr, returncode):
        self.stdout = stdout.decode().strip()
        self.stderr = stderr.decode().strip()
        self.returncode = returncode


class PlayerctlProcess:
    def __init__(self, proc, debug=False):
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
                    self.queue.put_nowait(line)

        async def printer(stream):
            while True:
                line = await stream.readline()
                print(line)
                if not line:
                    break

        asyncio.get_event_loop().create_task(reader(proc.stdout))
        asyncio.get_event_loop().create_task(printer(proc.stderr))

    def running(self):
        return self.proc.returncode is None


class PlayerctlCli:
    def __init__(self, bus_address=None, debug=False):
        self.bus_address = bus_address
        self.debug = debug
        self.proc = None

    async def _start(self, cmd):
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

    async def start(self, cmd):
        proc = await self._start(cmd)
        return PlayerctlProcess(proc)

    async def run(self, cmd):
        proc = await self._start(cmd)
        stdout, stderr = await proc.communicate()
        await proc.wait()
        return CommandResult(stdout, stderr, proc.returncode)
