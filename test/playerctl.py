import asyncio
import os
from shlex import join


# Subprocesses started by the tests. asyncio only closes a subprocess
# transport once the process has exited and every pipe has hit EOF, so these
# are stopped while the test's event loop is still running.
_processes = []


def register(proc, *tasks):
    _processes.append((proc, tasks))


async def stop_process(proc, *tasks):
    if proc.returncode is None:
        proc.terminate()
    await proc.wait()
    for task in tasks:
        await task
    for stream in (proc.stdout, proc.stderr):
        if stream is not None:
            await stream.read()


async def stop_all():
    while _processes:
        proc, tasks = _processes.pop()
        await stop_process(proc, *tasks)


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

        self._reader = asyncio.get_event_loop().create_task(
            reader(proc.stdout)
        )
        # asyncio.get_event_loop().create_task(printer(proc.stderr))
        register(proc, self._reader)

    def running(self):
        return self.proc.returncode is None

    async def stop(self):
        await stop_process(self.proc, self._reader)


class PlayerctlCli:
    def __init__(self, bus_address=None, debug=False):
        self.bus_address = bus_address
        self.debug = debug
        self.proc = None

    async def _start(self, cmd):
        env = os.environ.copy()
        shell_cmd = f'exec playerctl {cmd}'

        if self.bus_address:
            env['DBUS_SESSION_BUS_ADDRESS'] = self.bus_address
        if self.debug:
            env['G_MESSAGES_DEBUG'] = 'playerctl'

        return await asyncio.create_subprocess_shell(
            shell_cmd,
            env=env,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

    async def start(self, cmd):
        proc = await self._start(cmd)
        return PlayerctlProcess(proc)

    async def run(self, cmd):
        proc = await self._start(cmd)
        stdout, stderr = await proc.communicate()
        await proc.wait()
        return CommandResult(stdout, stderr, proc.returncode)

    async def list(self, players=[], ignored=[]):
        args = ['--list-all']

        if players:
            args.extend(['--player', ','.join(players)])

        if ignored:
            args.extend(['--ignored-players', ','.join(ignored)])

        cmd = await self.run(join(args))
        assert cmd.returncode == 0, cmd.stderr
        return cmd.stdout.splitlines()
