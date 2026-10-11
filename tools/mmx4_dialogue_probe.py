"""Inspect an existing private X4 runtime's native textbox.

Default mode uses no inputs, stage fixtures, or game RAM writes. --arm only
adds debug traces. --approach explicitly drives a Slash Beast dialogue test
on the supplied root-owned hidden runtime; it never launches a process.
"""
import argparse
import json
from pathlib import Path
import struct
import time

from coop_debug import Runtime

PLAY = 0x801721C0
TEXT = 0x8013BC28
PANEL_POINTER = 0x801397BC
PARTICLES = 0x80173CA0
PARTICLE_STRIDE = 0x60
PARTICLE_COUNT = 64
TEXT_INIT = 0x8002217C
SLASH_TEXT = 0x8006E260


def word(data, at=0):
    return struct.unpack_from('<I', data, at)[0]


def actor(data, address):
    return dict(address=hex(address), active=data[0], type=data[1],
                subtype=data[2], visible=data[3], state=list(data[4:7]),
                animation=data[0x17], frame=data[0x47],
                animation_flags=list(data[0x44:0x49]),
                x=struct.unpack_from('<i', data, 8)[0] / 65536,
                y=struct.unpack_from('<i', data, 12)[0] / 65536,
                clut=hex(struct.unpack_from('<H', data, 0x42)[0]),
                assembly=hex(word(data, 0x3C)))


def sample(runtime):
    play = runtime.read(PLAY, 0x64)
    text = runtime.read(TEXT, 0x14)
    pointers = runtime.read(PANEL_POINTER, 28)
    panel_address = word(pointers)
    pool = runtime.read(PARTICLES, PARTICLE_COUNT * PARTICLE_STRIDE)
    text_actors = []
    for i in range(PARTICLE_COUNT):
        data = pool[i * PARTICLE_STRIDE:(i + 1) * PARTICLE_STRIDE]
        if data[0] and data[1] == 22:
            text_actors.append(actor(data, PARTICLES + i * PARTICLE_STRIDE))
    panel = next((a for a in text_actors if int(a['address'], 16) == panel_address), None)
    palette_address = word(runtime.read(0x1F800028, 4))
    script_resource = word(runtime.read(0x1F800048, 4))
    palette_offset = 0x7C0 if play[12] == 13 else 0x2A0
    palette = runtime.read(palette_address + palette_offset, 64)
    # Native upload rectangle800F1658 is x0/y480/w256/h8. Each row is512
    # bytes, so gameplay palette+2A0 maps to glyphCLUT7845/panelCLUT7846.
    clut_x, clut_y = (224, 483) if play[12] == 13 else (80, 481)
    vram = runtime.request('vram_peek', x=clut_x, y=clut_y, w=32, h=1)['hex']
    vram_words = [int(vram[i:i + 4], 16) for i in range(0, len(vram), 4)]
    palette_words = list(struct.unpack('<32H', palette))
    native_palette = runtime.read(0x800F1700, 64)
    phase = text[13]
    return dict(frame=runtime.request('get_registers')['frame'],
                mode=list(play[:4]), stage=play[12], section=play[13],
                campaign=play[0x43], first_visit_policy=play[0x2E],
                text=dict(active=text[12], phase=phase, page=text[14],
                          timer=struct.unpack_from('<H', text, 10)[0],
                          script=hex(word(text)), header=hex(struct.unpack_from('<H', text, 4)[0])),
                text_actor_pointers=[hex(word(pointers, i)) for i in range(0, 28, 4)],
                text_actors=text_actors, panel=panel,
                panel_ready=bool(text[12] and phase != 6 and panel and
                                 panel['subtype'] == 2 and panel['visible'] and panel['frame'] != 255),
                palette_pointer=hex(palette_address), script_resource=hex(script_resource),
                palette_dirty=runtime.read(0x80166BB0, 1)[0],
                palette_matches_native=palette == native_palette,
                palette_matches_vram=palette_words == vram_words,
                vram_matches_native=list(struct.unpack('<32H', native_palette)) == vram_words,
                native_palette_words=list(struct.unpack('<32H', native_palette)),
                palette_words=palette_words, vram_words=vram_words)


def panel_qualified(row):
    return bool(row['panel_ready'] and row['palette_matches_native'] and
                row['palette_matches_vram'] and row['vram_matches_native'])


def arm(runtime):
    for target in (TEXT_INIT, SLASH_TEXT):
        runtime.request('fntrace_arm', target=hex(target))
    for lo, hi in ((0x001721EE, 0x001721EF), (0x0013BC32, 0x0013BC36),
                   (0x001397BC, 0x001397C0)):
        runtime.request('wtrace_arm', lo=hex(lo), hi=hex(hi))


def traces(runtime, frame_lo=None):
    fields = {} if frame_lo is None else dict(frame_lo=frame_lo)
    return dict(text_init=runtime.request('fntrace_dump', target_lo=hex(TEXT_INIT),
                                         target_hi=hex(TEXT_INIT + 4), count=64),
                slash_text=runtime.request('fntrace_dump', target_lo=hex(SLASH_TEXT),
                                          target_hi=hex(SLASH_TEXT + 4), count=128),
                policy_writes=runtime.request('wtrace_dump', count=128, newest=1,
                    addr_lo=hex(PLAY + 0x2E), addr_hi=hex(PLAY + 0x2F), **fields),
                writes=runtime.request('wtrace_dump', count=2048, **fields))


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def trace_diagnosis(recorded, baseline):
    """Explain observed arguments/writers without substituting native policy."""
    calls = sorted((e for e in recorded['text_init'].get('entries', [])
                    if number(e['seq']) >= baseline['functions'] and
                    number(e['a0']) == 0x10), key=lambda e: number(e['seq']))
    writes = sorted((e for e in recorded['policy_writes'].get('entries', [])
                     if number(e['seq']) >= baseline['writes']), key=lambda e: number(e['seq']))
    arguments = [dict(seq=e['seq'], frame=e.get('frame'), ra=e.get('ra'),
                      a0=e['a0'], a1=e['a1'], a2=e['a2'], a2_byte=number(e['a2']) & 255)
                 for e in calls]
    policy = [dict(seq=e['seq'], frame=e.get('frame'), pc=e.get('pc'),
                   cpu_pc=e.get('cpu_pc'), old=e.get('old'), new=e['new'],
                   new_byte=number(e['new']) & 255,
                   setup_seed=number(e['seq']) == baseline.get('host_seed_seq')) for e in writes]
    # write_ram's host store can inherit g_debug_last_store_pc. Its known
    # setup seed must not be attributed to that stale native producer PC.
    native_writes = [e for e in policy if not e['setup_seed'] and number(e.get('pc') or 0)]
    if not arguments:
        explanation = 'No Slash TextInit(a0=16) call survived in this trace window.'
    elif arguments[-1]['a2_byte']:
        explanation = ('The last observed Slash TextInit received nonzero a2. '
                       'For an inactive textbox, native 800223A0 selects phase6/timer60 '
                       'and bypasses panel/palette setup. An already active80 textbox instead '
                       'queues the request at800221A8 without initializing a new panel. '
                       'Slash reads PLAY+2E at8006E2B0 and writes1 at8006E2C4; '
                       'the recorded policy writer PCs identify earlier policy changes.')
    else:
        explanation = ('Slash TextInit received a2=0, requesting the full textbox path. '
                       'Absent visible-panel evidence is not explained by a nonzero first-visit argument; '
                       'inspect the initial TEXT state and recorded actor/phase writes.')
    return dict(text_init_arguments=arguments, policy_writes=policy,
                native_policy_writes=native_writes, explanation=explanation)


def slash_dialogue(scenarios, owners=(1, 0), timeout=30, capture_frames=4,
                   report_path=None, strict=True):
    """Scenarios-compatible native approach; P2-first precedes P1-first.

    The caller owns the hidden runtime. Each fresh fixture receives exactly
    one first-visit policy seed before crossing X9472. No textbox argument,
    actor, or later story-policy write is replaced by this test.
    """
    if timeout <= 0 or capture_frames < 1:
        raise ValueError('Positive timeout and at least one panel frame required')
    if not owners or len(owners) > 2 or len(set(owners)) != len(owners) or any(o not in (0, 1) for o in owners):
        raise ValueError('Choose one or both distinct seats0/1')
    runtime = scenarios.r
    output = Path(scenarios.output)
    output.mkdir(parents=True, exist_ok=True)
    report_path = Path(report_path) if report_path else output / 'slash-dialogue.json'
    cases = []
    result = dict(campaign=scenarios.campaign, cases=cases)
    for owner in owners:
        label = f'slash-dialogue-owner-{owner}'
        case = dict(owner=owner, samples=[], captured_panels=[], qualified=False,
                    ownership_observed=False, policy_seed_count=0, skipped_phase6_observed=False)
        cases.append(case)
        baseline = dict(functions=0, writes=0)
        begin_frame = None
        try:
            runtime.release()
            scenarios.load(8, 1, 3)
            runtime.teleport_both(9360, 395)
            start = scenarios.wait(label + '-grounded', lambda s:
                s['mode'][:2] == [6, 0] and not s['blocked'] and all(
                    p['grounded'] and p['hp'] and not p['script'] and not p['door']
                    for p in s['players']), 10)
            assert all(p['x'] < 9472 for p in start['players']), start
            case['start'] = start
            case['initial_text'] = sample(runtime)
            arm(runtime)
            before = traces(runtime)
            baseline = dict(functions=number(before['text_init'].get('total', 0)),
                            writes=number(before['policy_writes'].get('total', 0)))
            begin_frame = runtime.request('get_registers')['frame']
            runtime.write(PLAY + 0x2E, 0)
            case['policy_seed_count'] = 1
            case['seeded_policy'] = runtime.read(PLAY + 0x2E, 1)[0]
            assert case['seeded_policy'] == 0
            seed_trace = runtime.request('wtrace_dump', count=128, newest=1,
                addr_lo=hex(PLAY + 0x2E), addr_hi=hex(PLAY + 0x2F), frame_lo=begin_frame)
            case['policy_seed_trace'] = seed_trace
            seed_entries = sorted((e for e in seed_trace.get('entries', [])
                if number(e['seq']) >= baseline['writes'] and not (number(e['new']) & 255)),
                key=lambda e: number(e['seq']))
            if seed_entries:
                baseline['host_seed_seq'] = number(seed_entries[0]['seq'])
            runtime.input(0xFFDF, owner)
            moving = True
            deadline = time.monotonic() + timeout
            last_capture = 0
            while time.monotonic() < deadline:
                state = scenarios.sample(label + '-approach')
                row = sample(runtime)
                row['players'] = state['players']
                row['blocked'] = state['blocked']
                row['warning'] = state['warning']
                case['samples'].append(row)
                case['skipped_phase6_observed'] |= bool(row['text']['active'] and row['text']['phase'] == 6)
                assert all(p['hp'] for p in state['players']), state
                if state['players'][owner]['script'] or state['players'][owner]['door']:
                    if not case['ownership_observed']:
                        case['ownership_observed'] = True
                        case['entry'] = state
                        assert state['players'][owner ^ 1]['x'] < 9472, ('Lagging seat crossed gate', state)
                if moving and (state['blocked'] or any(p['script'] or p['door'] for p in state['players'])):
                    runtime.release()
                    moving = False
                if panel_qualified(row) and time.monotonic() - last_capture >= .10:
                    index = len(case['captured_panels'])
                    path = output / f'{label}-panel-{index}.png'
                    runtime.screenshot(path)
                    row['screenshot'] = str(path)
                    case['captured_panels'].append(row)
                    last_capture = time.monotonic()
                    if len(case['captured_panels']) >= capture_frames:
                        break
                if case['ownership_observed'] and not state['blocked'] and not row['text']['active'] and all(
                        p['active'] and p['visible'] and p['state'] == 1 and p['action'] >= 2 and
                        not p['script'] and not p['door'] for p in state['players']) and any(
                        e['type'] == 54 and e['hp'] > 1 for e in scenarios.enemies()):
                    # A genuine phase6 skip ends without a panel. Stop at the
                    # native fight rather than standing under attacks until
                    # the observation timeout or changing the story policy.
                    case['dialogue_ended_without_enough_panel_frames'] = True
                    break
                time.sleep(.015)
            case['qualified'] = bool(case['ownership_observed'] and
                                     len(case['captured_panels']) >= capture_frames)
            case['traces'] = traces(runtime, begin_frame)
            case['diagnosis'] = trace_diagnosis(case['traces'], baseline)
            if not case['qualified']:
                path = output / f'{label}-unqualified.png'
                runtime.screenshot(path)
                case['unqualified_screenshot'] = str(path)
            # Confirm only the initiating seat after recording panel evidence.
            # A skipped phase6 dialogue can also complete naturally here.
            runtime.release()
            deadline = time.monotonic() + 25
            while time.monotonic() < deadline:
                state = scenarios.sample(label + '-finish')
                enemies = scenarios.enemies()
                if not state['blocked'] and not state['freeze'] and not state['warning'] and \
                        not runtime.read(TEXT + 12, 1)[0] and any(
                        e['type'] == 54 and e['hp'] > 1 for e in enemies) and all(
                        p['active'] and p['visible'] and p['state'] == 1 and p['action'] == 2 and
                        p['grounded'] and p['hp'] and not p['script'] and not p['door']
                        for p in state['players']):
                    case['final'] = state
                    runtime.screenshot(output / f'{label}-fight.png')
                    break
                if state['blocked'] or runtime.read(TEXT + 12, 1)[0]:
                    runtime.input(0xBFFF, owner)
                    time.sleep(.04)
                    runtime.release()
                time.sleep(.025)
            else:
                raise TimeoutError(('Native Slash dialogue/fight did not release', state))
        except Exception as exc:
            case['error'] = f'{type(exc).__name__}: {exc}'
            case['qualified'] = False
            case['traces'] = traces(runtime, begin_frame)
            case['diagnosis'] = trace_diagnosis(case['traces'], baseline)
        finally:
            runtime.release()
            report_path.parent.mkdir(parents=True, exist_ok=True)
            report_path.write_text(json.dumps(result, indent=2), encoding='utf-8')
    if strict and not all(c['qualified'] and 'final' in c for c in cases):
        raise AssertionError(('Native visible Slash panel did not qualify; inspect report', str(report_path)))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--wait-panel', type=float, default=0)
    parser.add_argument('--approach', action='store_true',
                        help='Drive Slash native entry on this explicitly supplied root-owned port')
    parser.add_argument('--campaign', type=int, choices=(0, 1))
    parser.add_argument('--owners', type=int, choices=(0, 1), nargs='+', default=[1, 0])
    parser.add_argument('--approach-timeout', type=float, default=30)
    parser.add_argument('--panel-frames', type=int, default=4)
    args = parser.parse_args()
    if args.wait_panel < 0:
        parser.error('--wait-panel must be nonnegative')
    if args.approach and args.campaign is None:
        parser.error('--approach requires --campaign for the supplied root-owned runtime')
    if args.approach_timeout <= 0 or args.panel_frames < 1:
        parser.error('Positive --approach-timeout and --panel-frames required')
    if len(args.owners) > 2 or len(set(args.owners)) != len(args.owners):
        parser.error('--owners must contain distinct seats')
    runtime = Runtime(args.port)
    if args.approach:
        from test_mmx4_coop_regressions import Scenarios
        scenarios = Scenarios(runtime, args.output.parent, args.campaign)
        result = slash_dialogue(scenarios, args.owners, args.approach_timeout,
                                args.panel_frames, args.output)
        print(json.dumps(dict(campaign=result['campaign'], cases=[dict(
            owner=c['owner'], qualified=c['qualified'], panels=len(c['captured_panels']),
            diagnosis=c['diagnosis']) for c in result['cases']]), indent=2))
        return
    if args.arm:
        arm(runtime)
    rows = []
    deadline = time.monotonic() + args.wait_panel
    while True:
        row = sample(runtime)
        rows.append(row)
        if row['panel_ready'] or time.monotonic() >= deadline:
            break
        time.sleep(0.04)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(samples=rows, traces=traces(runtime)), indent=2), encoding='utf-8')
    if args.capture and row['panel_ready']:
        runtime.screenshot(args.output.with_suffix('.png'))
    print(json.dumps({k: row[k] for k in ('frame', 'first_visit_policy', 'text', 'panel_ready',
                                        'palette_matches_native', 'palette_matches_vram')}, indent=2))
    if args.wait_panel and not row['panel_ready']:
        raise SystemExit('No native visible textbox qualified; inspect the trace and first-visit policy.')


if __name__ == '__main__':
    main()
