// A MesenCE save state for the recomp: writes its field stream, which the game loads at launch.
//
//   node tools/compare/mesen-state.mts <movie.mmo | state.mss> <out>
//   SOE_COMPARE_MESEN_STATE=<out> build/SecretOfEvermoreSNESRecomp
//
// The state must sit at a field's line 225, as every state MesenCE saves between frames does. It holds memory from your
// own ROM: never commit it.
import { readFileSync, writeFileSync } from 'node:fs';
import { inflateState, stateOf } from './movie.mts';

const [input, output] = process.argv.slice(2);
if (!output) { console.error('usage: mesen-state.mts <movie.mmo | state.mss> <out>'); process.exit(2); }
const state = stateOf(readFileSync(input), /\.mmo$/i.test(input));
if (!state) { console.error('the movie has no save state'); process.exit(1); }
writeFileSync(output, inflateState(state));
