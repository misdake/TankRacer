import createModule from '../wasm/tankracer.mjs';
import {fetchBytes} from './loading.js';

export const maps=['ENGLAND','MEXICO','SWAMP','EAST','THEME','BIO','MONACO','CIRCUIT'];
const hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');

export class WasmEngine{
 constructor(module,manifest,readAsset,onProgress){this.module=module;this.manifest=manifest;this.readAsset=readAsset;this.onProgress=onProgress;this.assets=new Map();this.opponents=true;}
 static async create({readAsset,manifest,moduleOptions={},onProgress=()=>{}}={}){
  const root=new URL('../wasm/',import.meta.url);
  manifest??=await(await fetch(new URL('manifest.json',root))).json();
  readAsset??=async(entry,progress)=>fetchBytes(new URL(entry.path,root),entry.bytes,progress);
  if(!moduleOptions.wasmBinary){
   const entry={...manifest.files['tankracer.wasm'],path:'tankracer.wasm'},progress=loaded=>onProgress({key:'wasm',loaded,total:entry.bytes});
   const bytes=new Uint8Array(await readAsset(entry,progress));
   if(bytes.length!==entry.bytes||hex(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)))!==entry.sha256)throw Error('Wasm verification failed');
   progress(bytes.length);moduleOptions={...moduleOptions,wasmBinary:bytes};
  }
  return new WasmEngine(await createModule(moduleOptions),manifest,readAsset,onProgress);
 }
 async asset(name){
  if(!this.assets.has(name))this.assets.set(name,(async()=>{
   const entry=this.manifest.assets[name];if(!entry)throw Error('Unknown asset: '+name);
   const bytes=new Uint8Array(await this.readAsset(entry,loaded=>this.onProgress({key:'asset:'+name,loaded,total:entry.bytes})));
   if(bytes.byteLength!==entry.bytes||hex(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)))!==entry.sha256)throw Error('Asset verification failed: '+name);
   return bytes;
  })());
  try{const bytes=await this.assets.get(name);this.onProgress({key:'asset:'+name,loaded:bytes.length,total:bytes.length});return bytes;}catch(e){this.assets.delete(name);throw e;}
 }
 check(status,operation){if(status)throw Error(`${operation} failed (${status})`);}
 async reset({map='ENGLAND',cup='B',vehicle=0,mode=0,opponents=true}={}){
  if(!maps.includes(map)||!['B','S','G'].includes(cup)||!Number.isInteger(vehicle)||vehicle<0||vehicle>15||![0,1].includes(mode)||typeof opponents!=='boolean')throw Error('Invalid track or car selection');
  const names=['DATA','TANKS/BRONZE','TANKS/SILVER','TANKS/GOLD','INGAME','LEVELS/'+map,'COMMON'];
  const bytes=await Promise.all(names.map(name=>this.asset(name)));const m=this.module;
  this.check(m._tank_create(maps.indexOf(map),'BSG'.indexOf(cup),vehicle,mode,Number(opponents)),'initialization');
  this.opponents=opponents;
  try{
   for(let i=0;i<bytes.length;i++){
    const p=m._malloc(bytes[i].length);if(!p)throw Error('Not enough memory to load assets');
    try{m.HEAPU8.set(bytes[i],p);this.check(m._tank_mount(i,p,bytes[i].length),'mount '+names[i]);}finally{m._free(p);}
   }
   this.check(m._tank_start(),'track loading');return this.step(0,0,true);
  }catch(e){m._tank_destroy();throw e;}
 }
 step(count=1,bits=0,focused=true){
  if(!Number.isInteger(count)||count<0||count>8||!Number.isInteger(bits)||bits<0||bits>262143||typeof focused!=='boolean')throw Error('Invalid input frame');
  const m=this.module;this.check(m._tank_step(count,bits,Number(focused)),'update');
  const p=m._tank_packet(),n=m._tank_packet_size();return m.HEAPU8.slice(p,p+n).buffer;
 }
 snapshot(){const m=this.module,p=m._tank_data();return m.HEAPU8.slice(p,p+0x800000);}
 cupStatus(){const m=this.module,p=m._tank_data(),v=new DataView(m.HEAPU8.buffer),get=at=>v.getUint32(p+at,true);return {nextAllowed:!!get(0x5903fc),complete:!!get(0x590408),points:get(0x53e3a8),rank:get(0x53e3ac)};}
 audioGain(){return this.module._tank_audio_gain()/65535;}
 audioCommands(menu=false){const m=this.module,p=menu?m._tank_menu_audio():m._tank_audio(),count=menu?m._tank_menu_audio_count():m._tank_audio_count(),view=new DataView(m.HEAPU8.buffer);return Array.from({length:count},(_,i)=>Array.from({length:9},(_,j)=>view.getInt32(p+i*36+j*4,true)));}
 async menuInitialize(){
  if(this.menuReady)return;
  const bytes=await this.asset('GUI'),m=this.module,p=m._malloc(bytes.length);
  if(!p)throw Error('Not enough memory for the menu');
  try{m.HEAPU8.set(bytes,p);this.check(m._tank_menu_create(p,bytes.length),'menu loading');this.menuReady=true;}finally{m._free(p);}
 }
 async menuStep(bits=0,dt=16,typed=-1){await this.menuInitialize();const m=this.module;this.check(m._tank_menu_step(bits,dt,typed),'menu update');const p=m._tank_menu_packet(),n=m._tank_menu_packet_size();return m.HEAPU8.slice(p,p+n).buffer;}
 async menuOpen(){await this.menuInitialize();this.check(this.module._tank_menu_open(),'menu entry');this.close();return this.menuStep(0,0);}
 async menuFinish(){await this.menuInitialize();this.check(this.module._tank_menu_finish(),'race results');return this.menuStep(0,0);}
 async menuUnlock(){await this.menuInitialize();this.check(this.module._tank_menu_unlock(),'unlocking');return this.menuStep(0,0);}
 async advance({map,opponents=this.opponents}){
  if(!maps.includes(map)||typeof opponents!=='boolean')throw Error('Invalid next track.');
  const bytes=await this.asset('LEVELS/'+map),m=this.module,p=m._malloc(bytes.length);if(!p)throw Error('Not enough memory.');
  try{m.HEAPU8.set(bytes,p);this.check(m._tank_advance(maps.indexOf(map),p,bytes.length,Number(opponents)),'next race');this.opponents=opponents;}finally{m._free(p);}
  return this.step(0,0,true);
 }
 close(){this.module._tank_destroy();}
}
