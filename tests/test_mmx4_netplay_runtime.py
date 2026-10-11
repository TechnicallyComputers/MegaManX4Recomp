"""Offline two-peer models for synchronized fixture and host-seat routing."""
import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from mmx4_netplay_runtime import NetplayRuntime


class FakePeer:
    def __init__(self, port, address):
        self.port, self.address = port, address
        self.requests, self.reads, self.captures = [], [], []
        self.fail_input = False
        self.play = bytearray(0x20)
        self.play[0] = 6
        self.snapshot = bytearray(0x3E4)
        struct.pack_into('<5I', self.snapshot, 0, 0x5834434F, 123, 1, 0, 1)
        struct.pack_into('<I', self.snapshot, 0x1C, 1)
        for at, character in ((0x300, 0), (0x100, 1)):
            self.snapshot[at] = self.snapshot[at + 4] = 1
            self.snapshot[at + 2] = character
            self.snapshot[at + 5] = 2
            self.snapshot[at + 0x5C] = 32

    def request(self, cmd, **fields):
        self.requests.append((cmd, fields))
        if cmd == 'set_input':
            if self.fail_input:
                raise RuntimeError('fake input failure')
            if fields['buttons'] == '0xbfff':
                self.play[0x1C] = 0
        return dict(ok=True, cmd=cmd, fields=fields, source=self.port)

    def read(self, address, size):
        self.reads.append((address, size))
        if address == 0x801721C0:
            return bytes(self.play[:size])
        if address == self.address:
            return bytes(self.snapshot[:size])
        return bytes([self.port & 255]) * size

    def screenshot(self, path):
        self.captures.append(path)
        return dict(ok=True, path=str(path), source=self.port)


class FakeDevelopment:
    def __init__(self):
        self.addresses = [0x801F0000, 0x801F1000]
        self.peers = [FakePeer(101, self.addresses[0]), FakePeer(102, self.addresses[1])]
        self.fixtures, self.teleports = [], []

    def fixture(self, command, first=0, second=0, third=0, *, x=0, y=0):
        # This stands in for the admitted seven-word packet sender, never RAM.
        event = (command, first, second, third, x, y)
        self.fixtures.append(event)
        return event

    def teleport(self, seat, x, y):
        self.teleports.append((seat, x, y))
        return self.fixture(4, seat, x=x, y=y)


class NetplayRuntimeTest(unittest.TestCase):
    def setUp(self):
        self.connection_guard = patch('socket.create_connection',
                                      side_effect=AssertionError('Offline models must not connect'))
        self.connection_guard.start()
        self.addCleanup(self.connection_guard.stop)
        self.development = FakeDevelopment()
        self.runtime = NetplayRuntime(self.development)

    def test_construction_has_no_requests_and_reads_remain_on_view_peer_zero(self):
        first, second = self.development.peers
        self.assertFalse(first.requests or second.requests or first.reads or second.reads)
        self.assertEqual(self.runtime.diagnostic(), self.development.addresses[0])
        self.assertEqual(self.runtime.read(0x80010000, 4), b'eeee')
        result = self.runtime.request('mod_counters')
        self.assertEqual(result['source'], 101)
        self.assertEqual(self.runtime.screenshot(Path('scene.png'))['source'], 101)
        self.assertEqual(first.captures, [Path('scene.png')])
        self.assertFalse(second.requests or second.reads or second.captures)

    def test_stage_checkpoint_and_single_boundary_both_teleport_use_packet_sender(self):
        self.runtime.fixture(1, 8, 1, 1)
        self.runtime.fixture(3, 3)
        self.runtime.teleport(1, 9473, 395)
        self.runtime.teleport_both(9360, 395)
        self.assertEqual(self.development.teleports, [(1, 9473, 395)])
        self.assertEqual(self.development.fixtures, [(1, 8, 1, 1, 0, 0),
                         (3, 3, 0, 0, 0, 0), (4, 1, 0, 0, 9473, 395),
                         (4, 2, 0, 0, 9360, 395)])
        self.assertFalse(any(p.requests or p.reads for p in self.development.peers))

    def test_inputs_target_the_owning_peer_host_layer_with_no_simulation_port(self):
        self.runtime.input(0xFFDF, 1)
        self.runtime.input(0xBFFF, 0)
        for peer, buttons in zip(self.development.peers, ('0xbfff', '0xffdf')):
            self.assertEqual(peer.requests, [('set_input', dict(layer='host', buttons=buttons,
                              lx=128, ly=128, rx=128, ry=128))])
            self.assertNotIn('port', peer.requests[0][1])
        self.runtime.release()
        self.assertTrue(all(p.requests[-1][1]['buttons'] == '0xffff'
                            and p.requests[-1][1]['layer'] == 'host' for p in self.development.peers))

    def test_release_still_neutralizes_guest_when_host_request_fails(self):
        first, second = self.development.peers
        first.fail_input = True
        with self.assertRaisesRegex(RuntimeError, 'fake input failure'):
            self.runtime.release()
        self.assertEqual(first.requests[-1][1]['buttons'], '0xffff')
        self.assertEqual(second.requests[-1][1]['buttons'], '0xffff')

    def test_raw_setup_mutations_are_rejected_before_any_peer_request(self):
        with self.assertRaisesRegex(ValueError, 'RAM writes'):
            self.runtime.write(0x801721C0, 6)
        for cmd in ('write_ram', 'set_input', 'clear_input', 'call_function', 'load_state'):
            with self.assertRaisesRegex(ValueError, 'Direct mutations'):
                self.runtime.request(cmd, addr='0x801721c0')
        self.assertFalse(any(p.requests for p in self.development.peers))

    def test_invalid_seat_or_coordinates_never_reach_packet_sender(self):
        for seat, x, y in ((2, 56, 139), (0, 32768, 139), (1, 56.5, 139)):
            with self.assertRaises(ValueError):
                self.runtime.teleport(seat, x, y)
        with self.assertRaises(ValueError):
            self.runtime.teleport_both(56, -32769)
        with self.assertRaises(ValueError):
            self.runtime.input(0xFFDF, 2)
        with self.assertRaises(ValueError):
            self.runtime.input(0x10000, 0)
        self.assertFalse(self.development.fixtures or self.development.teleports)

    def test_wait_playable_reuses_completed_two_body_snapshot_without_ram_writes(self):
        players = self.runtime.wait_playable(timeout=1)
        self.assertEqual([p[2] for p in players], [0, 1])
        self.assertTrue(all(p[4] == 1 and p[5] == 2 and p[0x5C] == 32 for p in players))
        self.assertFalse(self.development.fixtures)
        self.assertFalse(self.development.peers[1].reads)

    def test_dialogue_confirmation_uses_host_input_then_releases_both_peers(self):
        self.development.peers[0].play[0x1C] = 1
        with patch('coop_debug.time.sleep'):
            self.runtime.wait_playable(timeout=1, advance_dialogue=True)
        first, second = self.development.peers
        self.assertTrue(any(fields['buttons'] == '0xbfff' for cmd, fields in first.requests))
        self.assertTrue(all(cmd == 'set_input' and fields['layer'] == 'host'
                            for peer in (first, second) for cmd, fields in peer.requests))
        self.assertTrue(all(peer.requests[-1][1]['buttons'] == '0xffff' for peer in (first, second)))
        self.assertFalse(self.development.fixtures)


if __name__ == '__main__':
    unittest.main()
