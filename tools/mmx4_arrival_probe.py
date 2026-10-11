"""Capture native X4 arrivals and P2 Intro entry on an existing private runtime.

Requires an explicitly supplied, root-owned TCP port. Never launches a process.
Stage/checkpoint and observed safe approach coordinates are setup fixtures;
controller inputs drive arrival completion, scene ownership, and Dash. Initial
stage capture starts before wait_playable so both native beams remain observable.
Screenshots require visual review: actor visibility and draw counters alone do
not establish that the complete character body rendered correctly.
"""
import argparse
import json
from pathlib import Path
import struct
import time

from coop_debug import Runtime
from test_mmx4_coop_regressions import Scenarios

RIGHT = 0xFFDF
LEFT = 0xFF7F
DASH = 0xDFFF
CONFIRM = 0xBFFF
ARRIVAL_FUNCTIONS = (0x80035848, 0x80031410, 0x800343A4)


def word(data, at):
    return struct.unpack_from('<I', data, at)[0]


def body(data):
    return dict(active=data[0], character=data[2], visible=data[3],
                state=data[4], action=data[5], phase=data[6], hp=data[0x5C] & 127,
                x=struct.unpack_from('<i', data, 8)[0] / 65536,
                y=struct.unpack_from('<i', data, 12)[0] / 65536,
                previous_y=struct.unpack_from('<i', data, 0x1C)[0] / 65536,
                velocity_y=struct.unpack_from('<i', data, 0x24)[0] / 65536,
                pose=data[0x17], animation=list(data[0x44:0x49]),
                assembly=hex(word(data, 0x3C)), table=hex(word(data, 0x38)),
                clut=hex(struct.unpack_from('<H', data, 0x42)[0]),
                collision_box=hex(word(data, 0x68)), geometry_wait=data[0xBE],
                grounded=bool(data[0x89] & 8),
                locks={hex(at): data[at] for at in (0x67, 0xBC, 0xC0, 0xC3, 0xC4)},
                vehicle=data[0xC5])


def incoming(player):
    return bool(player['active'] and player['state'] == 1 and player['action'] == 0)


def ordinary(player):
    return bool(player['active'] and player['visible'] and player['hp'] and
                player['state'] == 1 and 2 <= player['action'] < 16 and
                player['grounded'] and not any(player['locks'].values()) and
                not player['vehicle'])


def ready(row):
    return bool(row['completed'] and row['enrolled'] and row['mode'][:2] == [6, 0]
                and not row['blocked'] and not row['freeze'] and not row.get('warning', 0) and
                all(ordinary(p) for p in row['players']))


def standing(row):
    return bool(ready(row) and all(p['action'] == 2 for p in row['players']) and
                abs(row['players'][0]['y'] - row['players'][1]['y']) <= 1 and
                abs(row['players'][1]['x'] - row['players'][0]['x'] - 24) <= 1)


class Probe:
    def __init__(self, runtime, output, campaign, trace=False):
        self.r, self.output, self.campaign = runtime, output, campaign
        self.address = runtime.diagnostic()
        self.events, self.cases = [], []
        self.started = time.monotonic()
        self.scenarios = Scenarios(runtime, output, campaign)
        self.trace = trace
        if trace:
            # Add targets without resetting another worker's trace selection.
            for target in ARRIVAL_FUNCTIONS:
                runtime.request('fntrace_arm', target=hex(target))

    def sample(self, label, warning=False):
        # A single completed native boundary includes both canonical bodies
        # and PLAY. Raw PLAYER can contain projected P2 mid-callback.
        began = time.monotonic()
        mirror = self.r.read(self.address, 0x464)
        play = mirror[0x400:0x464]
        row = dict(label=label, seconds=time.monotonic() - self.started,
                   read_seconds=time.monotonic() - began, frame=word(mirror, 4),
                   completed=word(mirror, 0x1C) == 1, enrolled=word(mirror, 8),
                   failed=word(mirror, 12), fixture_ack=word(mirror, 0x38),
                   fixture_result=word(mirror, 0x54), mode=list(play[:4]),
                   stage=play[12], section=play[13], checkpoint=play[0x1D],
                   blocked=play[0x1C], freeze=play[0x10],
                   spawn_policy=play[0x1E], spawn_started=play[0x1F],
                   players=[body(mirror[at:at + 0xE4]) for at in (0x300, 0x100)])
        if warning:
            row['warning'] = self.r.read(0x80141BDC, 1)[0]
        self.events.append(row)
        if row['failed']:
            raise AssertionError(('Co-op renderer failed', row))
        return row

    def capture(self, label, row=None, pools=False):
        row = row or self.sample(label)
        if pools:
            row['native_pools'] = {}
            for name, address, stride, count in (
                    ('small', 0x801410C0, 0x60, 20),
                    ('ui', 0x801435B0, 0x60, 32),
                    ('particles', 0x80173CA0, 0x60, 64),
                    ('stage', 0x80142F98, 0x30, 32)):
                data = self.r.read(address, stride * count)
                records = []
                for slot in range(count):
                    p = data[slot * stride:(slot + 1) * stride]
                    if p[0]:
                        records.append(dict(slot=slot, address=hex(address + slot * stride),
                            type=p[1], subtype=p[2], active=p[0], visible=p[3], state=list(p[4:7]),
                            x=struct.unpack_from('<i', p, 8)[0] / 65536,
                            y=struct.unpack_from('<i', p, 12)[0] / 65536, raw=p.hex()))
                row['native_pools'][name] = records
        path = self.output / (label + '.png')
        self.r.screenshot(path)
        row.setdefault('screenshots', []).append(str(path))
        return row

    def counters(self):
        return {c['name']: c['count'] for c in self.r.request('mod_counters')['counters']}

    def stage_request(self, stage, section=0):
        # Commit the same native fixture mailbox used by Runtime.fixture,
        # then poll acknowledgement together with the beam observations.
        # Runtime.fixture's 50ms acknowledgement wait can miss short beams.
        sequence = word(self.r.read(self.address + 0x30, 4), 0) + 1
        if sequence > 255:
            raise RuntimeError('Private process exhausted its fixture sequences')
        for offset, value in enumerate((1, stage, section, self.campaign)):
            self.r.write(self.address + 0x34 + offset, value)
        self.r.write(self.address + 0x30, sequence)
        return sequence

    def healthy(self, row):
        if row['completed'] and row['enrolled']:
            # Native state3 also represents the living outgoing warp. It is
            # expected for a partner leaving a scripted arena approach.
            # Require positive health here and ordinary visible bodies at
            # completion rather than treating that transient warp as death.
            assert all(p['hp'] for p in row['players']), row

    def wait_ready(self, label, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            row = self.sample(label, warning=True)
            self.healthy(row)
            if ready(row):
                return row
            time.sleep(.008)
        raise TimeoutError((label, row))

    def initial(self, stage, section, arrival_seat='first', timeout=35, exercise_movement=True):
        self.r.release()
        if not exercise_movement:
            self.r.input(0xFFFF, 0)
            self.r.input(0xFFFF, 1)
        live = self.r.read(0x801721C0, 2)
        if live[0] != 6:
            raise RuntimeError('Arm initial-stage probe from an existing gameplay dispatcher')
        label = f'initial-stage-{stage}-{section}'
        before = self.counters()
        sequence = self.stage_request(stage, section)
        if exercise_movement:
            # Stress mode preserves the original held-input setup: a body
            # can begin walking before its partner completes the beam.
            self.r.input(RIGHT, 0)
            self.r.input(RIGHT, 1)
        deadline = time.monotonic() + timeout
        overlap = first_eligible = dash_begin = dash_end = None
        dash_seat, actual_dash, dash_overlap = None, False, False
        saw_new_incoming, released, overlap_captured = False, not exercise_movement, False
        touchdown_captured, descent_captured = False, False
        try:
            while time.monotonic() < deadline:
                row = self.sample(label)
                if row['fixture_ack'] != sequence:
                    time.sleep(.003)
                    continue
                if row['fixture_result'] == 2:
                    raise RuntimeError(('Native stage fixture rejected', row))
                if row['stage'] != stage or row['section'] != section or not row['completed']:
                    time.sleep(.003)
                    continue
                saw_new_incoming |= any(incoming(p) for p in row['players'])
                if not saw_new_incoming:
                    time.sleep(.003)
                    continue
                self.healthy(row)
                if overlap is None and all(incoming(p) and p['visible'] for p in row['players']):
                    overlap = row
                    gap = row['players'][1]['x'] - row['players'][0]['x']
                    assert abs(gap - 24) <= 1, ('Stage-load horizontal gap is not 24px', row)
                    self.capture(label + '-concurrent-beams', row, pools=not exercise_movement)
                if overlap is not None and not descent_captured and all(
                        incoming(p) and not p['phase'] for p in row['players']) and any(
                        p['y'] - overlap['players'][seat]['y'] >= 24
                        for seat, p in enumerate(row['players'])):
                    descent_captured = True
                    self.capture(label + '-native-descent', row, pools=not exercise_movement)
                if not touchdown_captured and all(incoming(p) for p in row['players']) and any(
                        p['phase'] for p in row['players']):
                    touchdown_captured = True
                    self.capture(label + '-native-touchdown', row, pools=not exercise_movement)
                if not exercise_movement:
                    if standing(row):
                        break
                    time.sleep(.003)
                    continue
                eligible = [seat for seat, p in enumerate(row['players'])
                            if ordinary(p) and not row['blocked']]
                if eligible and first_eligible is None:
                    first_eligible = dict(seats=eligible, observation=row)
                    dash_seat = eligible[0] if arrival_seat == 'first' else int(arrival_seat)
                if dash_begin is None and dash_seat in eligible:
                    self.r.input(RIGHT & DASH, dash_seat)
                    dash_begin, dash_end = row, time.monotonic() + .14
                if dash_begin is not None and row['players'][dash_seat]['action'] in (12, 13):
                    actual_dash = True
                    if incoming(row['players'][dash_seat ^ 1]):
                        dash_overlap = True
                        if not overlap_captured:
                            overlap_captured = True
                            row['dash_overlap_capture'] = True
                            self.capture(label + '-dash-during-partner-beam', row)
                if dash_end is not None and time.monotonic() >= dash_end and not released:
                    self.r.release()
                    released = True
                if released and ready(row):
                    break
                time.sleep(.003)
            else:
                raise TimeoutError(('Initial beam or native completion not observed', row))
        finally:
            self.r.release()
        if exercise_movement:
            final = self.capture(label + '-finished', self.wait_ready(label + '-ready'))
        else:
            # The stress predicate permits walking/Dash and is insufficient
            # for this visual fixture. Keep both neutral through this exact
            # standing observation; action8 plus stale ground cannot pass.
            assert standing(row), ('Neutral native arrivals did not stand together', row)
            final = self.capture(label + '-finished', row, pools=True)
        after = self.counters()
        delta = after.get('mmx4.coop.concurrent-stage-arrivals', 0) - before.get(
            'mmx4.coop.concurrent-stage-arrivals', 0)
        assert [p['character'] for p in final['players']] == [self.campaign, self.campaign ^ 1]
        result = dict(kind='initial', stage=stage, section=section, concurrent_counter_delta=delta,
                      exercise_movement=exercise_movement,
                      native_descent_captured=descent_captured,
                      native_touchdown_captured=touchdown_captured,
                      concurrent_beams_observed=overlap is not None,
                      initial_gap_pixels=(overlap['players'][1]['x'] - overlap['players'][0]['x']
                                          if overlap is not None else None),
                      first_eligible=first_eligible, dash_seat=dash_seat,
                      native_dash_observed=actual_dash, dash_during_partner_beam=dash_overlap,
                      dash_begin=dash_begin, final=final,
                      beam_evidence='qualified' if overlap is not None and delta else 'inconclusive',
                      dash_overlap_evidence='qualified' if dash_overlap else 'inconclusive')
        if exercise_movement:
            assert actual_dash, ('Dash input never became a native action', result)
        self.cases.append(result)

    def timing(self, dash_seat, active):
        before = self.counters()
        start = self.r.request('get_registers')['frame']
        begin = time.monotonic()
        try:
            while time.monotonic() - begin < 2:
                if active:
                    direction = RIGHT if int((time.monotonic() - begin) * 4) % 2 else LEFT
                    self.r.input(direction & DASH, dash_seat)
                time.sleep(.04)
                self.r.release()
                time.sleep(.04)
        finally:
            self.r.release()
        elapsed = time.monotonic() - begin
        end = self.r.request('get_registers')['frame']
        after = self.counters()
        name = 'mmx4.coop.primary-private-character-draws'
        return dict(dashing=active, seconds=elapsed, debug_frames=end - start,
                    debug_frames_per_second=(end - start) / elapsed,
                    primary_private_draws=after.get(name, 0) - before.get(name, 0),
                    phase=self.r.request('phase_profile', window=2))

    def intro(self, dash_seat):
        label = f'intro-p2-entry-dash-{dash_seat}'
        self.scenarios.load(0, 1, 2)
        self.r.teleport(0, 4200, 443)
        before = self.capture(label + '-approach')
        assert not before['blocked'] and not any(p['locks']['0xc0'] for p in before['players']), before
        deadline = time.monotonic() + 65
        owner_seen, dragon_seen, attempted, accepted, overlap = False, False, False, False, False
        own_beam_attempt = False
        pulse_end = 0
        after_scene = False
        last_enemy_check = 0
        try:
            while time.monotonic() < deadline:
                row = self.sample(label, warning=True)
                self.healthy(row)
                owner_seen |= bool(row['players'][1]['locks']['0xc0'] or
                                   row['players'][1]['locks']['0xc4'])
                now = time.monotonic()
                if now - last_enemy_check >= .12:
                    dragon_seen |= any(e['type'] == 8 and e['hp'] > 1 for e in self.scenarios.enemies())
                    last_enemy_check = now
                if row['blocked']:
                    self.r.input(CONFIRM, 0)
                    self.r.input(CONFIRM, 1)
                    time.sleep(.03)
                    self.r.release()
                elif dragon_seen:
                    after_scene = True
                    target, partner = row['players'][dash_seat], row['players'][dash_seat ^ 1]
                    if not attempted and (incoming(target) or incoming(partner)):
                        attempted, own_beam_attempt = True, incoming(target)
                        self.r.input(LEFT & DASH, dash_seat)
                        pulse_end = now + .14
                        self.capture(label + '-arrival-dash-input', row)
                    if attempted and target['action'] in (12, 13):
                        accepted = True
                        overlap |= incoming(partner)
                    if pulse_end and now >= pulse_end:
                        self.r.release()
                        pulse_end = 0
                    if ready(row) and not pulse_end:
                        break
                else:
                    # Original Mac conversation returns Zero to4112. Restore
                    # only the observed safe pre-dragon platform after the
                    # scene ends; P2 input must cross the actual native gate.
                    first = row['players'][0]
                    if self.campaign == 1 and first['x'] < 4200 and first['action'] == 2 and \
                            not first['locks']['0xc0']:
                        self.r.teleport(0, 4200, 442)
                    self.r.input(RIGHT, 1)
                time.sleep(.005)
            else:
                raise TimeoutError(('P2 Intro entry/arrival did not finish', row))
        finally:
            self.r.release()
        assert owner_seen and dragon_seen and after_scene, ('Native P2 event ownership missing', row)
        before_dash = self.wait_ready(label + '-both-ready')
        # Place the two healthy bodies apart on the observed arena floor so
        # a screenshot can distinguish missing graphics from ordinary overlap.
        # The original P2 event and partner arrival have already completed.
        base = max(p['x'] for p in before_dash['players'])
        for seat in (0, 1):
            self.r.teleport(seat, int(base + 24 + seat * 96),
                            int(before_dash['players'][seat]['y']))
        self.r.input(LEFT & DASH, dash_seat)
        later_dash = False
        try:
            deadline = time.monotonic() + .18
            while time.monotonic() < deadline:
                row = self.sample(label + '-after-arrival-dash')
                self.healthy(row)
                if row['players'][dash_seat]['action'] in (12, 13):
                    later_dash = True
                    self.capture(label + '-native-dash', row)
                    break
                time.sleep(.005)
        finally:
            self.r.release()
        assert later_dash, ('Post-arrival Dash never became native action', row)
        final = self.capture(label + '-finished', self.wait_ready(label + '-final'))
        timings = [self.timing(dash_seat, active) for active in (False, True)]
        post_timing = self.capture(label + '-post-timing', self.wait_ready(label + '-post-timing-ready'))
        self.cases.append(dict(kind='intro', dash_seat=dash_seat, p2_owned_event=owner_seen,
                               native_dragon_seen=dragon_seen, arrival_input_attempt=attempted,
                               input_during_own_beam=own_beam_attempt,
                               native_dash_during_arrival=accepted,
                               native_dash_during_partner_beam=overlap,
                               overlap_evidence='qualified' if overlap else 'inconclusive',
                               post_arrival_native_dash=later_dash, before_dash=before_dash,
                               final=final, post_timing=post_timing, timings=timings))

    def report(self, error=None):
        result = dict(campaign=self.campaign, port=self.r.port, cases=self.cases,
                      error=error, events=self.events,
                      interpretation='Review captured body images; debug frame rates are not displayed FPS.')
        if self.trace:
            result['native_traces'] = {hex(target): self.r.request(
                'fntrace_dump', target_lo=hex(target), target_hi=hex(target + 4), count=128)
                for target in ARRIVAL_FUNCTIONS}
        (self.output / 'arrival-probe.json').write_text(json.dumps(result, indent=2), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--campaign', type=int, choices=(0, 1), required=True)
    parser.add_argument('--mode', choices=('initial', 'intro', 'both'), default='both')
    parser.add_argument('--stages', type=int, choices=(1, 6), nargs='+', default=[1, 6])
    parser.add_argument('--sections', type=int, choices=(0, 1), nargs='+', default=[0])
    parser.add_argument('--arrival-seat', choices=('first', '0', '1'), default='first')
    parser.add_argument('--neutral', action='store_true',
                        help='Keep both seats neutral through native beams and standing 24px apart')
    parser.add_argument('--intro-seats', type=int, choices=(0, 1), nargs='+', default=[1, 0])
    parser.add_argument('--trace', action='store_true')
    parser.add_argument('--invulnerable', action='store_true',
                        help='Use the existing native invulnerability fixture for both seats')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    runtime = Runtime(args.port)
    probe = Probe(runtime, args.output, args.campaign, args.trace)
    error = None
    try:
        if args.invulnerable:
            runtime.fixture(5, 3)
        if args.mode in ('initial', 'both'):
            for stage in args.stages:
                for section in args.sections:
                    probe.initial(stage, section, args.arrival_seat, exercise_movement=not args.neutral)
        if args.mode in ('intro', 'both'):
            for seat in args.intro_seats:
                probe.intro(seat)
    except Exception as exc:
        error = f'{type(exc).__name__}: {exc}'
        raise
    finally:
        runtime.release()
        probe.report(error)
    print(json.dumps(dict(cases=probe.cases, output=str(args.output)), indent=2), flush=True)


if __name__ == '__main__':
    main()
