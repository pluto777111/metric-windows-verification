"""Prepare the user's saved selection and verify that every entry linked."""
import json
from pathlib import Path
import sys

action, workspace, output = sys.argv[1:]
workspace, output = Path(workspace), Path(output)
saved = json.loads((workspace / 'custom-37-layout.json').read_bytes())
baseline = json.loads((workspace / 'metric-layout.json').read_bytes())
expected = [*saved['selected'], 'metric']
if len(set(expected)) != 38 or saved['memory'] != 'expanded' or saved['reserved_bytes'] != 4194304:
    raise ValueError('Expected the saved 37-machine selection and Open-XY 4 MiB mode')
numbers = baseline['machines']
if any(numbers.get(machine) != identity for machine, identity in saved['machines'].items()):
    raise ValueError('The saved custom layout and METRIC layout disagree')
if numbers['metric'] != {'engine': 163, 'page': 139}:
    raise ValueError('METRIC numbering changed')

if action == 'prepare':
    from tooling.machines import machine_at
    from tooling.extensions import extension_at

    catalog = Path('local/catalog').resolve()
    source = json.loads((catalog / 'metric-collection.json').read_bytes())
    entries = [machine_at(catalog / item['path']).id for item in source['machines']]
    extensions = [extension_at(catalog / item['path']).id for item in source['extensions']]
    if entries != saved['selected'] or extensions != saved['extensions']:
        raise ValueError('The pinned source archive differs from the saved selection')
    # The expanded profile and linker set memory for this build. Pass the
    # numerical map to the native CLI, which reads layout identities separately.
    Path('local/combined-layout.json').write_text(json.dumps(baseline), encoding='utf-8')
elif action == 'verify':
    manifest = json.loads((output / 'manifest.json').read_bytes())
    layout = json.loads((output / 'layout.json').read_bytes())
    report = json.loads((output / 'analysis/compile-report.json').read_bytes())
    verification = json.loads((output / 'verification.json').read_bytes())
    if layout['machines'] != numbers or manifest['layout'] != numbers:
        raise ValueError('Machine IDs changed during the build')
    if verification['linked'] != expected or report['linked'] != expected:
        raise ValueError('The full saved selection and METRIC must be linked: ' + repr(report['linked']))
    if verification['single_shared_reservation_bytes'] != 4194304 or not verification['fresh_source_rebuild_matches']:
        raise ValueError('The expanded image did not verify independently')
    if not {'ott_insert', 'drumline', 'drum_sends', 'extensions'} <= set(report['services']):
        raise ValueError('Missing OTT or a baseline SDK service')
    stack = report.get('audio_stack_bytes', {})
    if 'sdk:master_filter' not in stack:
        raise ValueError('No mix OTT audio entry in the native stack audit')
    if not all(any(entry.get('extension', '').replace('\\', '/').endswith('/' + item)
                   for entry in manifest['collection']) for item in saved['extensions']):
        raise ValueError('A saved extension is absent')
    summary = dict(layout, selected=expected, extensions=saved['extensions'],
                   memory='expanded', reserved_bytes=4194304)
    (output / 'expanded-layout.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(f"Verified {len(expected)} machines, {len(saved['extensions'])} extensions and OTT. "
          f"Module span {report['sizes']['allocated_span_bytes']} bytes.")
else:
    raise ValueError('Unknown verification action')
