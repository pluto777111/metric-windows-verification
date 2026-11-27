"""Report static Blackfin assembly counts and check for wide-multiply helper calls."""
from pathlib import Path
import re
import sys

assembly = next(Path(sys.argv[1]).rglob('ott_insert.s'), None)
if not assembly:
    raise SystemExit('The native build did not retain ott_insert.s.')
source = assembly.read_text(encoding='utf-8')
if re.search(r'\b(call|jump|jump\.l)\b[^\n]*muldi3', source, re.IGNORECASE):
    raise SystemExit('Unexpected muldi3 call in the OTT adapter')
if not re.search(r'^\s*R\d+\s*=\s*R\d+\s*\*\s*R\d+;', source, re.M | re.I):
    raise SystemExit('No native rounded fractional multiply found in the OTT adapter')

marker = re.search(r'^([^:\s]*process_channelsILm2ELb1EE[^:\s]*):\s*$', source, re.M)
if not marker:
    raise SystemExit('Could not find the stereo Mix processor assembly')
end = source.find('\n\t.size\t' + marker[1], marker.end())
if end == -1:
    raise SystemExit('Could not find the end of the stereo Mix processor assembly')
instructions = sum(
    bool(re.match(r'\s*[^.#/\s]', line))
    for line in source[marker.end():end].splitlines() if line.startswith('\t')
)
print(f'Stereo Mix: {instructions} static assembly lines across all branches')
print('Rounded fractional multiply present, no muldi3 calls. These counts are not cycles.')
