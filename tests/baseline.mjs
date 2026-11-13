// Optional export/release regression: compare with any earlier built wasm folder.
// Neither production builds nor the normal test suite require a baseline.
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {WasmEngine,maps} from '../dist/src/wasm-engine.js';
import {decodeNativePacket} from '../dist/src/native-packet.js';
if(!process.argv[2])throw Error('Usage: node tests/baseline.mjs /path/to/previous/site/wasm');
const previous=path.resolve(process.argv[2]),current=path.resolve('dist/wasm');
async function engine(root,baseline=false){
 const manifest=JSON.parse(await readFile(path.join(root,'manifest.json'),'utf8')),readAsset=e=>readFile(path.join(root,e.path));
 if(!baseline)return WasmEngine.create({manifest,readAsset});
 const factory=(await import(pathToFileURL(path.join(root,'tankracer.mjs')))).default;
 return new WasmEngine(await factory({wasmBinary:await readFile(path.join(root,'tankracer.wasm'))}),manifest,readAsset,()=>{});
}
const a=await engine(current),b=await engine(previous,true),stable=m=>Object.fromEntries(Object.entries(m).filter(([k])=>!['simulation_ms','command_ms'].includes(k)));
const report={passed:false,race_states:0,menu_states:0,game_updates:0,maps:[],baseline_wasm:b.manifest.files['tankracer.wasm'].sha256};
function compare(left,right,label){
 const x=decodeNativePacket(left),y=decodeNativePacket(right);assert.deepEqual(stable(x.metadata),stable(y.metadata),label+' metadata');
 assert.deepEqual(Buffer.from(x.commands.buffer,x.commands.byteOffset,x.commands.byteLength),Buffer.from(y.commands.buffer,y.commands.byteOffset,y.commands.byteLength),label+' TCB1');
 assert.deepEqual(x.textures,y.textures,label+' textures');
}
function memory(menu,label){
 const ptr=menu?'_tank_menu_data':'_tank_data',x=a.module.HEAPU8.subarray(a.module[ptr](),a.module[ptr]()+0x800000),y=b.module.HEAPU8.subarray(b.module[ptr](),b.module[ptr]()+0x800000);
 assert.ok(Buffer.from(x).equals(Buffer.from(y)),label+' full DATA');assert.deepEqual(a.audioCommands(menu),b.audioCommands(menu),label+' audio commands');
}
try{
 compare(await a.menuOpen(),await b.menuOpen(),'menu open');memory(true,'menu open');report.menu_states++;
 // Class/car/route selection and all transition phases before starting a race.
 for(const bits of [2,0,1,0,16,0,...Array(40).fill(0),2,0,1,0,16,0,...Array(40).fill(0),16,0]){
  compare(await a.menuStep(bits),await b.menuStep(bits),'menu '+report.menu_states);memory(true,'menu');report.menu_states++;
 }
 for(const map of maps){
  const cup=map==='CIRCUIT'?'G':['EAST','BIO','MONACO'].includes(map)?'S':'B',options={map,cup};
  compare(await a.reset(options),await b.reset(options),map+' startup');memory(false,map+' startup');report.race_states++;
  const actions=map==='ENGLAND'?[...Array.from({length:27},()=>[8,1]),[8,9],[8,5],[1,16],[2,8193],[1,16385],[1,32769],[1,65537],[1,131072],[1,0],[1,32],[2,32],[1,0],[1,32],[1,0],[1,1024],[0,0],[1,0]]:[[8,1],[8,9]];
  for(const [n,bits]of actions){compare(a.step(n,bits),b.step(n,bits),map+' update');memory(false,map+' update');report.race_states++;report.game_updates+=n;}
  report.maps.push(map);console.log('BASELINE:',map,'full DATA, commands, textures and audio match');
 }
 report.passed=true;console.log(`BASELINE: PASS ${report.race_states} race states, ${report.menu_states} menu states`);
}finally{a.close();b.close();await mkdir('build/tests',{recursive:true});await writeFile('build/tests/baseline.json',JSON.stringify(report,null,2));}
