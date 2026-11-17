import assert from 'node:assert/strict';
import {Keys} from '../browser/keys.js';

const keys = new Keys();
keys.down('KeyA', true);
assert.equal(keys.handbrake, false);
keys.down('KeyA', false); // auto-repeat after returning to the race
assert.equal(keys.handbrake, false);
keys.up('KeyA');
keys.down('KeyA', false);
assert.equal(keys.handbrake, true);
keys.down('KeyA', true); // opening inspection does not cancel a held race action
assert.equal(keys.handbrake, true);
keys.up('KeyA');
assert.equal(keys.handbrake, false);
keys.down('KeyA', false);
keys.down('ArrowUp', false);
keys.clear(); // blur/visibility change
assert.equal(keys.handbrake, false);
assert.equal(keys.has('KeyA'), false);
assert.equal(keys.has('ArrowUp'), false);
console.log('KEYS: PASS (press ownership, repeat, view switching, release, blur)');
