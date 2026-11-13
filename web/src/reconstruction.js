import {NativeRenderer} from './native-renderer.js';
import {GameKeyboard} from './keyboard.js';
import {WasmSession} from './wasm-session.js';
import {maps} from './wasm-engine.js';
import {GameAudio,MENU_SOUNDS} from './game-audio.js';
import {GameTools} from './game-tools.js';
import {LoadingScreen} from './loading.js';

const loading=new LoadingScreen(),cachedAssets=new Set(),canvas=document.getElementById('game'),renderer=new NativeRenderer(canvas),keyboard=new GameKeyboard(),wasm=new WasmSession({onProgress:p=>{loading.progress(p);if(p.key.startsWith('asset:')&&p.loaded===p.total)cachedAssets.add(p.key.slice(6));}}),audio=new GameAudio();
const state={view:'loading',busy:false,frame:null,menu:null,generation:0,menuBits:0,pauseBits:0,requestBits:0,terminal:false,pending:null,noOpponents:false};
let previous=performance.now(),accumulator=0;
// These are external keyboard bindings. C517CB0 owns edge latches/event queues;
// C519000/518200 own selection and page transitions. There are no HTML menus.
const menuKeys={KeyA:1,ArrowLeft:1,KeyD:2,ArrowRight:2,KeyW:4,ArrowUp:4,KeyS:8,ArrowDown:8,Enter:16,Escape:32,Space:64};
const held=new Set(),characters=[];
const gameTools=new GameTools({onOpen:clear,onOpponents:enabled=>{state.noOpponents=enabled;},onUnlock:()=>new Promise((resolve,reject)=>{state.pending={kind:'unlock',resolve,reject};void pending();})});
function downloadPlan(manifest,assets,sounds){const packs=[...new Set(sounds.map(name=>manifest.sounds[name].pack))];return [...assets.filter(name=>!cachedAssets.has(name)).map(name=>({key:'asset:'+name,bytes:manifest.assets[name].bytes})),...packs.filter(name=>!audio.packs.has(name)).map(name=>({key:'soundpack:'+name,bytes:manifest.sound_packs[name].bytes}))];}
function keys(){state.menuBits=0;for(const code of held)state.menuBits|=menuKeys[code]||0;}
function clear(){held.clear();characters.length=0;keys();keyboard.clear();state.pauseBits=0;}
function fail(e){clear();loading.finish();gameTools.update(false);audio.stopAll();state.view='error';document.body.dataset.view='error';document.getElementById('error').textContent=e.message||String(e);document.getElementById('error').hidden=false;}
function receive(packet){
 const m=packet.metadata;state.frame=m;
 if(m.frontend){
  gameTools.update(true,!!m.unlocked);
  if(state.view!=='menu'){clear();audio.stopAll(true);renderer.clearTextures();}
  audio.apply(packet.audio,false,packet.gain);state.view='menu';state.menu=m;renderer.loadRGBA(packet.textures);renderer.draw({decoded_commands:packet.commands});
  Object.assign(document.body.dataset,{view:'menu',ready:'true',menuPage:String(m.page),menuSelection:String(m.selection),menuPhase:String(m.phase),physics:'wasm-c'});
  if(m.start)state.pending={kind:'race',options:m};
  return;
 }
 gameTools.update(false);
 if(state.generation!==m.generation){renderer.clearTextures();audio.stopAll(true);state.generation=m.generation;}
 renderer.loadRGBA(packet.textures);renderer.draw({decoded_commands:packet.commands});audio.apply(packet.audio,!!m.paused,packet.gain);
 state.view=m.paused?'paused':'playing';Object.assign(document.body.dataset,{view:state.view,ready:'true',nativeStep:String(m.frame),generation:String(m.generation),physics:'wasm-c',renderer:'raw-webgl',commandFormat:'TCB1',inputBits:String(m.input_bits),paused:String(m.paused)});
 if(m.action===3){clear();audio.stopAll(true);state.terminal=true;state.view='loading';state.pending={kind:'finish'};}
 else if(m.action>=2){clear();audio.stopAll(true);state.terminal=false;state.view='loading';state.pending={kind:'open'};}
 else if(m.action===1)state.pending={kind:'step-zero'};
}
async function pending(){
 if(state.busy||!state.pending)return;const next=state.pending;state.pending=null;state.busy=true;
 try{
  if(next.kind==='race'){
   gameTools.update(false);
   clear();audio.stopAll(true);state.view='loading';const m=next.options;
   const map=maps[m.map],continuation=state.terminal&&m.continuation,manifest=await audio.catalog(),sounds=Object.keys(manifest.sounds),assets=continuation?['LEVELS/'+map]:['DATA','TANKS/BRONZE','TANKS/SILVER','TANKS/GOLD','INGAME','LEVELS/'+map,'COMMON'];
   loading.begin('Loading '+map,downloadPlan(manifest,assets,sounds));loading.root.dataset.phase='level';
   const options={map,cup:'BSG'[m.cup],vehicle:m.vehicle,mode:0,opponents:!state.noOpponents};
   const [initial]=await Promise.all([continuation?wasm.advance(options):wasm.reset(options),audio.preload(sounds,p=>loading.progress(p))]);
   state.terminal=false;renderer.clearTextures();state.generation=initial.metadata.generation;renderer.loadRGBA(initial.textures);receive(await wasm.step({count:1,bits:0,focused:true}));
   loading.finish();
  }else if(next.kind==='unlock'){receive(await wasm.menuUnlock());next.resolve();}
  else if(next.kind==='finish')receive(await wasm.menuFinish());
  else if(next.kind==='open')receive(await wasm.menuOpen());
  else receive(await wasm.step({count:0,bits:0,focused:true}));
 }catch(e){next.reject?.(e);fail(e);}finally{state.busy=false;void pending();}
}
function pauseInput(){
 let bits=0;for(const code of held){const v=menuKeys[code];if(v===1)bits|=1;else if(v===2)bits|=2;else if(v===4)bits|=128;else if(v===8)bits|=256;else if(v===16)bits|=64;else if(v===64)bits|=512;else if(v===32)bits|=32;}return bits;
}
async function step(){
 if(state.busy)return;state.busy=true;
 try{
  if(state.view==='menu')receive(await wasm.menuStep(state.menuBits,16,characters.shift()??-1));
  else {const bits=(state.view==='paused'?pauseInput():keyboard.sample())|state.requestBits;state.requestBits=0;receive(await wasm.step({count:1,bits,focused:true}));}
 }catch(e){fail(e);}finally{state.busy=false;void pending();}
}
addEventListener('keydown',e=>{
 if(gameTools.ownsKey(e))return;
 if(e.metaKey||e.ctrlKey||e.altKey)return;
 if(state.view==='menu'&&state.menu?.phase===9&&e.key.length===1&&!e.repeat&&e.code!=='Space'){characters.push(e.key.charCodeAt(0));e.preventDefault();}
 if(menuKeys[e.code]){held.add(e.code);keys();}
 if(state.view==='menu'||state.view==='paused'){if(menuKeys[e.code]){e.preventDefault();audio.unlock();}return;}
 if(state.view!=='playing')return;
 if(e.code==='Escape'){e.preventDefault();if(!e.repeat)state.requestBits|=32;}
 else if(keyboard.down(e.code)){audio.unlock();e.preventDefault();}
});
addEventListener('keyup',e=>{held.delete(e.code);keys();const handled=keyboard.up(e.code)||menuKeys[e.code];if(!gameTools.ownsKey(e)&&handled)e.preventDefault();});
function blur(){clear();audio.apply([],true);if(state.view==='playing')state.requestBits|=32;}
addEventListener('blur',blur);document.addEventListener('visibilitychange',()=>{if(document.hidden)blur();});addEventListener('pagehide',()=>wasm.close());
function loop(now){const dt=Math.min(100,now-previous);previous=now;if(['menu','playing','paused'].includes(state.view)){accumulator=Math.min(16,accumulator+dt);if(accumulator>=16&&!state.busy){accumulator=0;void step();}}else accumulator=0;requestAnimationFrame(loop);}
async function start(){const manifest=await audio.catalog();loading.begin('Loading game',[{key:'wasm',bytes:manifest.files['tankracer.wasm'].bytes},...downloadPlan(manifest,['GUI'],MENU_SOUNDS)]);loading.root.dataset.phase='startup';const [packet]=await Promise.all([wasm.menuStep(0,0),audio.preload(MENU_SOUNDS,p=>loading.progress(p))]);receive(packet);loading.finish();}
state.busy=true;start().catch(fail).finally(()=>{state.busy=false;});requestAnimationFrame(loop);
export const app={state,receive,audio,wasm};
