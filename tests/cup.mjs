// Short finish-branch inputs from the qualified native lifecycle test.
// Only fixtures set lap/finish inputs; production UI never writes game DATA.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import path from 'node:path';
import {WasmEngine,maps} from '../dist/src/wasm-engine.js';
import {decodeNativePacket} from '../dist/src/native-packet.js';
const root=path.resolve('dist/wasm'),out=path.resolve('build/tests');await mkdir(out,{recursive:true});
const engine=await WasmEngine.create({manifest:JSON.parse(await readFile(path.join(root,'manifest.json'),'utf8')),readAsset:e=>readFile(path.join(root,e.path))});
const report={passed:false,races:[],original_C_menu_flow:true,fixture:'native lifecycle finish branch, original C results and live cross-track reload'};
const read=at=>new DataView(engine.module.HEAPU8.buffer).getUint32(engine.module._tank_data()+at,true);
const put=(at,value,size=4)=>{const v=new DataView(engine.module.HEAPU8.buffer),p=engine.module._tank_data()+at;size===1?v.setUint8(p,value):v.setUint32(p,value,true);};
let menu;const menuSamples=new Set();
const menuRead=at=>new DataView(engine.module.HEAPU8.buffer).getUint32(engine.module._tank_menu_data()+at,true);
async function menuStep(bits=0,dt=16,typed=-1){menu=decodeNativePacket(await engine.menuStep(bits,dt,typed)).metadata;for(const c of engine.audioCommands(true))if(c[0]===1||c[0]===2)menuSamples.add(c[2]>>>0);return menu;}
async function tap(bits=16){await menuStep(bits);return menuStep(0);}
async function settle(){for(let i=0;i<500&&[3,11].includes(menu.phase);i++)await menuStep();assert.equal(menu.phase,1);}
async function startCup(cup=0){menu=decodeNativePacket(await engine.menuOpen()).metadata;assert.equal(menu.page,2);for(let i=0;i<cup;i++)await tap(2);assert.equal(menu.selection,cup);await tap();await settle();assert.equal(menu.page,[3,26,28][cup]);await tap();await settle();assert.equal(menu.page,[4,27,29][cup]);await tap();assert.equal(menu.start,1);return menu;}
async function results(){menu=decodeNativePacket(await engine.menuFinish()).metadata;
 if(menu.phase===9){for(const ch of 'ABC')await menuStep(0,16,ch.charCodeAt(0));assert.equal(menuRead(0x530458)&0xffffff,0x434241);await tap();assert.equal(menu.phase,12);report.initials=true;}
 assert.equal(menu.phase,12);assert.equal(menu.page,17);
 for(let i=0;i<6&&menu.phase===12&&menuRead(0x562bfc)<5;i++)await tap();
 return menu;
}
try{
 for(const [cup,tracks]of [['ENGLAND','MEXICO','SWAMP','THEME'],['EAST','SWAMP','MONACO','MEXICO','BIO'],['MONACO','EAST','THEME','ENGLAND','BIO','CIRCUIT']].entries()){
 const chosen=await startCup(cup);await engine.reset({map:maps[chosen.map],cup:'BSG'[chosen.cup],vehicle:chosen.vehicle});let previousPoints=0;
 for(const [round,map]of tracks.entries()){
  // Let normal camera publication/fade run; no forced render state.
  for(let i=0;i<9;i++)engine.step(8,0);
  if(cup===0&&round===0){
   const gain=engine.audioGain();engine.step(1,32);engine.step(1,0);assert.equal(read(0x53e96c),1);
   for(let i=0;i<3;i++){engine.step(1,256);engine.step(1,0);}assert.equal(read(0x53e970),2);
   engine.step(1,64);engine.step(1,0);assert.equal(read(0x53e978),1);engine.step(8,1);assert.ok(engine.audioGain()<gain);
   engine.step(1,0);engine.step(1,64);engine.step(1,0);assert.equal(read(0x53e978),0);engine.step(1,32);engine.step(1,0);assert.equal(read(0x53e96c),0);report.pause_items=3;report.sound_fx_volume=true;
  }
  const player=read(0x53e8b8),obj=read(read(read(0x55c694)+player*8)+4);
  put(obj+0xdda,3,1);put(obj+0xddb,0,1);put(obj+0xdea,1,1);put(obj+0xddd,1,1);put(obj+0xcb6,3);
  for(let lap=0;lap<3;lap++){put(obj+0xcbe+lap*7,60000+lap*1000);for(let j=0;j<3;j++)put(obj+0xcc2+lap*7+j,j===0?1:0,1);}
  engine.step(1,0);for(let i=0;i<16;i++)engine.step(1,0);put(0x53e98c,9999);
  const terminal=engine.step(1,0),packet=decodeNativePacket(terminal);assert.equal(packet.metadata.action,3);assert.ok(packet.metadata.result_rank>=1);assert.equal(packet.metadata.result_time,183000);
  assert.ok(packet.metadata.result_points>0);assert.equal(read(0x53e3a8),previousPoints+packet.metadata.result_points);previousPoints=read(0x53e3a8);
  report.races.push({cup:'BSG'[cup],map,rank:packet.metadata.result_rank,time:packet.metadata.result_time,points:previousPoints});
  if(round===0){await writeFile(path.join(out,'finish-metadata.json'),JSON.stringify(packet.metadata,null,2));await writeFile(path.join(out,'finish-cup.json'),JSON.stringify(engine.cupStatus(),null,2));}
  await results();
  if(round<tracks.length-1){assert.equal(menu.phase,11);await settle();await tap();assert.equal(menu.start,1);assert.equal(menu.continuation,1);const next=decodeNativePacket(await engine.advance({map:maps[menu.map]}));assert.equal(read(0x53e3a8),previousPoints);assert.equal(read(0x530458)&0xffffff,0x434241);assert.equal(next.metadata.generation,round+2);assert.ok(next.textures.length>0);assert.equal(next.metadata.action,0);}
  else {assert.equal(menu.phase,cup===2?13:14);assert.equal(menuRead(0x53d190),Math.min(cup+1,2));if(cup===2){await menuStep(0,1000);assert.equal(menu.phase,13);report.gold_credits=true;}await tap();assert.equal(menu.phase,1);assert.equal(menu.page,2);await tap(2);assert.equal(menu.selection,1);report.silver_unlocked=true;}

 }
 assert.equal(read(0x590408),1);await assert.rejects(()=>engine.advance({map:'ENGLAND'}));
 }
 // Actual original qualification gate: last place earns no points and cannot
 // continue to Mexico. Entering the next round must not bypass this in JS.
 await startCup();await engine.reset({map:'ENGLAND',cup:'B',vehicle:menu.vehicle});for(let i=0;i<9;i++)engine.step(8,0);
 let player=read(0x53e8b8),obj=read(read(read(0x55c694)+player*8)+4);put(obj+0xdda,3,1);put(obj+0xdea,1,1);put(obj+0xddd,8,1);put(obj+0xcb6,3);
 engine.step(1,0);for(let i=0;i<16;i++)engine.step(1,0);player=read(0x53e8b8);obj=read(read(read(0x55c694)+player*8)+4);put(obj+0xddd,8,1);put(0x53e98c,9999);
 const failed=decodeNativePacket(engine.step(1,0));assert.equal(failed.metadata.action,3);assert.equal(engine.cupStatus().nextAllowed,false);await assert.rejects(()=>engine.advance({map:'MEXICO'}));
 await writeFile(path.join(out,'unqualified.json'),JSON.stringify({metadata:failed.metadata,cup:engine.cupStatus()},null,2));await results();assert.equal(menu.phase,12);assert.equal(menuRead(0x562bfc),5);assert.equal(menu.start,0);await tap();assert.equal(menu.page,2);assert.equal(menu.phase,1);report.qualification_gate=true;
 for(const name of ['select7','select15','select14','engine1']){let hash=2166136261;for(const ch of name)hash=Math.imul(hash^ch.charCodeAt(0),16777619)>>>0;assert.ok(menuSamples.has(hash),'menu sample '+name);}report.menu_audio_samples=menuSamples.size;
 report.passed=true;console.log('WASM CUP: PASS original C class/car/route menus, initials,15 finish/results/reloads, congratulations,Gold credits,class unlocks and qualification failure');
}finally{engine.close();await writeFile(path.join(out,'cup.json'),JSON.stringify(report,null,2));}
