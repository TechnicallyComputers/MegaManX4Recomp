"""Runtime-style access to an already admitted private development match.

Construct from an existing NetplayDevelopment; no process launch, connection,
or requests occur during construction. Setup fixtures use its synchronized
input packets. Gameplay input targets each peer's local host input layer.
"""
from coop_debug import Runtime


class NetplayRuntime(Runtime):
    READ_COMMANDS = frozenset((
        'read_ram', 'mod_counters', 'get_registers', 'gpu_state', 'netplay_status',
        'phase_profile', 'vram_peek', 'fntrace_dump', 'wtrace_dump',
        'screenshot_hires', 'screenshot_wide_hires', 'host_osd_shot',
    ))

    def __init__(self, development):
        if len(development.peers) != 2 or len(development.addresses) != 2:
            raise ValueError('Requires an existing two-peer NetplayDevelopment')
        self.development = development
        self.peers = tuple(development.peers)
        super().__init__(self.peers[0].port)

    def request(self, cmd, **fields):
        if cmd not in self.READ_COMMANDS:
            raise ValueError('Direct mutations are forbidden; use synchronized fixtures or seat input')
        return self.peers[0].request(cmd, **fields)

    def read(self, address, size):
        return self.peers[0].read(address, size)

    def write(self, address, value):
        raise ValueError('RAM writes are forbidden in the netplay adapter')

    def diagnostic(self):
        return self.development.addresses[0]

    def fixture(self, command, first=0, second=0, third=0):
        return self.development.fixture(command, first, second, third)

    @staticmethod
    def _coordinates(x, y):
        if not all(isinstance(v, int) and -32768 <= v < 32768 for v in (x, y)):
            raise ValueError('Coordinates must be signed integer world pixels')

    def teleport(self, seat, x, y):
        if seat not in (0, 1):
            raise ValueError('Seat must be 0 or 1')
        self._coordinates(x, y)
        return self.development.teleport(seat, x, y)

    def teleport_both(self, x, y):
        self._coordinates(x, y)
        # Native fixture4/seat2 commits both coordinates at one world boundary.
        return self.development.fixture(4, 2, x=x, y=y)

    def input(self, buttons=0xFFFF, seat=0):
        if seat not in (0, 1) or not isinstance(buttons, int) or not 0 <= buttons <= 0xFFFF:
            raise ValueError('Provide active-low 16-bit buttons and seat 0 or 1')
        return self.peers[seat].request('set_input', layer='host', buttons=hex(buttons),
                                        lx=128, ly=128, rx=128, ry=128)

    def release(self):
        errors = []
        for seat in (0, 1):
            try:
                self.input(0xFFFF, seat)
            except Exception as error:
                errors.append(error)
        if errors:
            raise errors[0]

    def screenshot(self, path):
        return self.peers[0].screenshot(path)
