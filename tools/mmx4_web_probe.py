"""Qualify real Lightning Web wall jumps on an existing private X4 runtime.

Requires root's explicit TCP port; never launches a process. Stage/weapon and
observed safe-platform teleports set up each case. Native Square creates the
web, and direction plus Jump must produce original action 10 and its actual
trajectory. No collision flags, projectile state, or jump state are written.
The optional train probe checks both saved Split backgrounds after native entry.
"""
import argparse
import json
from pathlib import Path
import struct
import time

from coop_debug import Runtime
from test_mmx4_coop_regressions import Scenarios

SHOTS = 0x801406F8
WEB_SOLID = 0x80108800
WEB_MAGIC = 0x57454231
STRIDE = 0x9C
RIGHT, LEFT, JUMP, SQUARE = 0xFFDF, 0xFF7F, 0xBFFF, 0x7FFF


def word(data, offset=0):
    return struct.unpack_from('<I', data, offset)[0]


def fixed(data, offset):
    return struct.unpack_from('<i', data, offset)[0] / 65536


def body(data):
    return dict(active=data[0], visible=data[3], character=data[2],
                state=data[4], action=data[5], phase=data[6], hp=data[0x5C] & 127,
                x=fixed(data, 8), y=fixed(data, 12),
                vx=fixed(data, 0x20), vy=fixed(data, 0x24),
                control=data[0x67], sides=data[0x71], used_sides=data[0x89],
                wall_kind=data[0x4A], solid=hex(word(data, 0x68)),
                grounded=bool(data[0x89] & 8), vehicle=data[0xC5],
                holds=[data[o] for o in (0x79, 0xBC, 0xC0, 0xC3, 0xC4)])


def web(data, slot):
    return dict(slot=slot, active=data[0], type=data[1], state=list(data[4:7]),
                x=fixed(data, 8), y=fixed(data, 12),
                solid=hex(word(data, 0x68)), enabled=data[0x75],
                owner_sides=data[0x72], owner_carried=data[0x76])


def opened(data):
    return bool(data[0] and data[1] == 1 and data[0x75] and word(data, 0x68) == WEB_SOLID)


class Probe:
    def __init__(self, runtime, output, campaign):
        self.r, self.output, self.campaign = runtime, output, campaign
        self.address = runtime.diagnostic()
        self.x_seat, self.zero_seat = campaign, campaign ^ 1
        self.scenarios = Scenarios(runtime, output, campaign)
        self.events, self.cases = [], []
        self.query_mirror = None

    def counters(self):
        return {c['name']: c['count'] for c in self.r.request('mod_counters')['counters']}

    def sample(self, label, shots=False):
        raw = self.r.read(self.address, 0x79C)
        if word(raw, 0x1C) != 1 or word(raw, 12):
            raise RuntimeError('A healthy completed canonical diagnostic boundary is required')
        play = raw[0x400:0x464]
        counts = self.counters()
        self.query_mirror = counts.get('mmx4.coop.zero-web-body-mirror', self.query_mirror)
        row = dict(label=label, frame=word(raw, 4), mode=list(play[:4]),
                   stage=play[12], section=play[13], blocked=play[0x1C], freeze=play[0x10],
                   players=[body(raw[o:o + 0xE4]) for o in (0x300, 0x100)],
                   queries=counts.get('mmx4.coop.zero-web-queries', 0),
                   side_contacts=counts.get('mmx4.coop.zero-web-side-contacts', 0))
        if self.query_mirror:
            row['last_query'] = body(self.r.read(self.query_mirror, 0xE4))
        if shots:
            if self.x_seat == 0:
                pool = self.r.read(SHOTS, 16 * STRIDE)
                row['webs'] = [web(p, i) for i in range(16)
                               if opened(p := pool[i * STRIDE:(i + 1) * STRIDE])]
            else:
                if word(raw, 0x6F0) != WEB_MAGIC:
                    raise RuntimeError('P2 inspection requires root\'s WEB1 diagnostic snapshot at +6F0/+700')
                slot = word(raw, 0x6F4)
                p = raw[0x700:0x79C]
                row['web_frame'] = word(raw, 0x6F8)
                row['webs'] = [web(p, slot)] if slot < 16 and opened(p) else []
        if row['mode'][0] == 6 and any(not p['hp'] or p['state'] >= 2 for p in row['players']):
            raise AssertionError(('Unsafe native setup: a player died', row))
        self.events.append(row)
        return row

    def capture(self, label, row=None):
        row = row or self.sample(label)
        path = self.output / (label + '.png')
        self.r.screenshot(path)
        row.setdefault('screenshots', []).append(str(path))
        return row

    def wait(self, label, predicate, timeout, shots=False):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            row = self.sample(label, shots)
            if predicate(row):
                return row
            time.sleep(.004)
        raise TimeoutError((label, row))

    def fixture_platform(self):
        self.scenarios.load(1, 0)
        self.r.fixture(7, self.x_seat, 1, 1)  # Native Lightning Web selection/unlock fixture.
        self.r.teleport_both(56, 139)
        self.r.teleport(self.zero_seat, 88, 139)
        self.wait('web-platform-ready', lambda s: all(p['grounded'] and p['action'] == 2 and
                  not any(p['holds']) and not p['vehicle'] for p in s['players']), 8)
        row = self.sample('web-platform-roster')
        assert [p['character'] for p in row['players']] == [self.campaign, self.campaign ^ 1], row

    def walljump(self, side):
        self.fixture_platform()
        # Facing is established through native input on the observed Jungle
        # platform. A brief Square press creates one actual weapon projectile.
        self.r.input(RIGHT, self.x_seat)
        time.sleep(.025)
        self.r.release()
        self.r.input(SQUARE, self.x_seat)
        time.sleep(.025)
        self.r.release()
        opened_row = self.wait('web-opened-' + side, lambda s: bool(s['webs']), 4, True)
        anchor = opened_row['webs'][0]
        zero = opened_row['players'][self.zero_seat]
        web_box = struct.unpack('<bbBB', self.r.read(WEB_SOLID, 4))
        zero_box = struct.unpack('<bbBB', self.r.read(int(zero['solid'], 16), 4))
        sign = -1 if side == 'left' else 1
        # Align the real collision box centers, with one pixel outside their
        # horizontal contact edge. Direction input must establish the contact.
        zero_x = round(anchor['x'] + web_box[0] - zero_box[0] +
                       sign * (web_box[2] + zero_box[2] + 1))
        zero_y = round(anchor['y'] + web_box[1] - zero_box[1])
        if not (32 <= zero_x <= 256 and 75 <= zero_y <= 139):
            raise AssertionError(('Opened web leaves the observed safe platform column', anchor,
                                  dict(zero_x=zero_x, zero_y=zero_y, web_box=web_box, zero_box=zero_box)))
        if abs(zero_x - opened_row['players'][self.x_seat]['x']) > 220:
            raise AssertionError('Opened web would exceed Unified partner spacing')
        self.r.teleport(self.zero_seat, zero_x, zero_y)
        before = self.sample('web-zero-adjacent-' + side, True)
        baseline = before['side_contacts']
        toward = RIGHT if side == 'left' else LEFT
        self.r.input(toward, self.zero_seat)
        try:
            touched = self.wait('web-native-contact-' + side, lambda s:
                s['side_contacts'] > baseline and s.get('last_query', {}).get('sides', 0) & 3 and
                s['last_query']['wall_kind'] > 0, .8, True)
            if not any(w['slot'] == anchor['slot'] for w in touched['webs']):
                raise AssertionError('The real web expired before the wall-jump input')
            self.r.input(toward & JUMP, self.zero_seat)
            launched = self.wait('web-native-walljump-' + side, lambda s:
                s['players'][self.zero_seat]['action'] == 10 and
                s['players'][self.zero_seat]['vy'] > 0, .6)
            self.capture('web-action10-' + side, launched)
        finally:
            self.r.release()
        trajectory = []
        deadline = time.monotonic() + .35
        while time.monotonic() < deadline:
            trajectory.append(self.sample('web-trajectory-' + side))
            time.sleep(.004)
        # Native action10 must move upward and away from the touched surface.
        away = [s for s in [launched] + trajectory if
                sign * (s['players'][self.zero_seat]['x'] - zero_x) > 1 and
                s['players'][self.zero_seat]['y'] < zero_y - 2]
        assert away, ('No upward/away native wall-jump trajectory', side, launched, trajectory)
        result = dict(side=side, x_seat=self.x_seat, zero_seat=self.zero_seat,
                      real_web=anchor, web_box=web_box, zero_box=zero_box,
                      setup=[zero_x, zero_y], contact=touched, action10=launched,
                      trajectory=away[0], status='passed')
        self.cases.append(result)
        return result

    def train_background(self, owner, seconds):
        self.scenarios.load(8, 1, 3)
        self.r.fixture(5, 3)  # Shared actors still run; prevent combat knockback during idle measurement.
        try:
            self.r.teleport_both(9360, 395)
            self.scenarios.wait('train-background-grounded', lambda s:
                                all(p['grounded'] and p['hp'] for p in s['players']), 8)
            self.r.input(RIGHT, owner)
            try:
                self.scenarios.wait('train-background-native-entry', lambda s:
                    s['blocked'] or any(p['script'] for p in s['players']), 15)
            finally:
                self.r.release()
            self.scenarios.wait('train-background-fight-ready', lambda s:
                not s['blocked'] and not s['freeze'] and not s['warning'] and
                any(e['type'] == 54 and e['hp'] > 1 for e in self.scenarios.enemies()) and
                all(p['active'] and p['visible'] and p['state'] == 1 and p['action'] == 2 and
                    p['hp'] and not p['door'] and not p['script'] for p in s['players']), 30, True)
            counts = self.counters()
            address = counts.get('mmx4.coop.split-state')
            if not address:
                raise RuntimeError('This train comparison requires actual Split cameras')
            rows = []
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                raw = self.r.read(address, 16 + 2 * 0xFC)
                row = self.sample('train-idle-owner-' + str(owner))
                row['camera_owner'] = word(raw, 8)
                row['views'] = [[dict(x=fixed(raw, 16 + seat * 0xFC + layer * 0x54 + 8),
                                     y=fixed(raw, 16 + seat * 0xFC + layer * 0x54 + 12))
                                 for layer in range(3)] for seat in range(2)]
                rows.append(row)
                if len(rows) == 1:
                    actors=self.r.read(0x80169CB8,4*0x30)
                    row['native_background_actors']=[dict(slot=i,type=p[1],state=list(p[4:8]),
                        timer=p[0x14],speed=hex(word(p,0x18)))
                        for i in range(4) for p in [actors[i*0x30:(i+1)*0x30]] if p[0]]
                    self.capture('train-background-before-' + str(owner), row)
                time.sleep(.04)
            self.capture('train-background-after-' + str(owner), rows[-1])
            assert len(rows) >= 2, 'No native train measurement window'
            changes = [[rows[-1]['views'][seat][layer]['x'] - rows[0]['views'][seat][layer]['x']
                        for layer in range(3)] for seat in range(2)]
            assert all(any(abs(d) > .125 for d in seat[1:]) for seat in changes), \
                ('A saved scenery pane remains frozen', owner, changes, rows[0], rows[-1])
            assert all(abs(seat[0]) <= 1 for seat in changes), \
                ('Foreground moved during the idle background comparison', changes)
            result = dict(owner=owner, changes_x=changes, rows=rows, status='passed')
            self.cases.append(result)
            return result
        finally:
            self.r.release()
            self.r.fixture(5, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--campaign', type=int, choices=(0, 1), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--scenario', choices=('web', 'train', 'both'), default='web')
    parser.add_argument('--side', choices=('left', 'right', 'both'), default='both')
    parser.add_argument('--owner', choices=('0', '1', 'both'), default='both')
    parser.add_argument('--seconds', type=float, default=2)
    args = parser.parse_args()
    if not (1 <= args.port <= 65535) or not (0.2 <= args.seconds <= 30):
        parser.error('Provide a TCP port and a measurement window between .2 and 30 seconds')
    args.output.mkdir(parents=True, exist_ok=True)
    probe = Probe(Runtime(args.port), args.output, args.campaign)
    report = dict(campaign=args.campaign, scenario=args.scenario,
                  method='native controller-driven projectiles, contact and jumps; setup fixtures only')
    try:
        if args.scenario in ('web', 'both'):
            for side in ('left', 'right') if args.side == 'both' else (args.side,):
                probe.walljump(side)
        if args.scenario in ('train', 'both'):
            for owner in (1, 0) if args.owner == 'both' else (int(args.owner),):
                probe.train_background(owner, args.seconds)
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', error=repr(error))
        raise
    finally:
        probe.r.release()
        report.update(cases=probe.cases, events=probe.events)
        (args.output / 'stage-interactions.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps(dict(status=report.get('status'), campaign=args.campaign,
                              cases=len(probe.cases), output=str(args.output)), indent=2))


if __name__ == '__main__':
    main()
