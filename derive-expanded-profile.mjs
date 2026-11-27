// Use the exact expanded geometry validated by the SDK browser builder.
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { expandedProfile, PROFILE_SHA256 } from './builder/core.mjs';

const profilePath = 'profiles/opxy-1.2.1.json';
const linkerPath = 'linker/opxy-1.2.1.ld';
const digest = data => createHash('sha256').update(data).digest('hex');
const sourceProfile = readFileSync(profilePath);
if (digest(sourceProfile) !== PROFILE_SHA256) throw new Error('The OTT profile differs from its reviewed digest');
const profile = JSON.parse(sourceProfile);
const expanded = expandedProfile(profile);
const mixHooks = new Set(['sdk_master_buttons', 'sdk_master_shift_buttons',
  'sdk_master_turn', 'sdk_master_frame', 'sdk_master_process']);
for (const hook of expanded.patches) {
  if (!mixHooks.delete(hook.symbol)) continue;
  if (hook.service !== 'master_filter') throw new Error('Unexpected stock MIX hook');
  hook.service = 'ott_insert';
}
if (mixHooks.size) throw new Error('Missing verified MIX EQ hook sites');
const sourceLinker = readFileSync(linkerPath);
if (digest(sourceLinker) !== profile.linker_sha256) throw new Error('The stock linker script differs from the profile');
const region = p => `ORIGIN = 0x${p.module_base.toString(16)}, LENGTH = 0x${(p.reserved_bytes - p.table_bytes).toString(16)}`;
const linker = sourceLinker.toString().replace(region(profile), region(expanded))
  .replace('the 2 MiB tail', 'the 4 MiB tail')
  .replace(`[0x${profile.module_base.toString(16)},`, `[0x${expanded.module_base.toString(16)},`);
if (!linker.includes(region(expanded)) || linker.includes(region(profile))) throw new Error('Expanded linker region missing');
expanded.linker_sha256 = digest(linker);
const finalProfile = JSON.stringify(expanded, null, 2) + '\n';
const builderPath = 'builder/core.mjs';
const builder = readFileSync(builderPath, 'utf8').replace(
  `export const PROFILE_SHA256 = '${PROFILE_SHA256}';`,
  `export const PROFILE_SHA256 = '${digest(finalProfile)}';`);
writeFileSync(linkerPath, linker);
writeFileSync(profilePath, finalProfile);
writeFileSync(builderPath, builder);
