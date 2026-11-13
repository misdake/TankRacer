// PCM and playback are host services. C supplies all game cue decisions.
const root=new URL('../wasm/',import.meta.url);
import {fetchBytes} from './loading.js';
export const MENU_SOUNDS=['select15','select7','select8','select12','select9','slider','select13','select14','select1','engine1'];
const hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
export class GameAudio{
 constructor(){this.voices=new Map();this.buffers=new Map();this.samples=new Map();this.packs=new Map();this.events=0;this.played=0;this.generation=0;this.errors=[];}
 catalog(){return this.manifest??=fetch(new URL('manifest.json',root)).then(r=>{if(!r.ok)throw Error('Audio catalog could not be loaded.');return r.json();});}
 unlock(){
  if(!this.context){this.context=new AudioContext();this.master=this.context.createGain();this.master.gain.value=.35;this.master.connect(this.context.destination);this.menuMaster=this.context.createGain();this.menuMaster.gain.value=.35;this.menuMaster.connect(this.context.destination);}
  void this.catalog();
  void this.context.resume();
 }
 async pack(name,onProgress=()=>{}){
  const manifest=await this.catalog(),meta=manifest.sound_packs[name];if(!meta)throw Error('Unknown audio pack '+name);
  let state=this.packs.get(name);
  if(!state){
   state={loaded:0,listeners:new Set(),promise:null};this.packs.set(name,state);
   state.promise=(async()=>{
    const bytes=await fetchBytes(new URL(meta.path,root),meta.bytes,loaded=>{state.loaded=loaded;for(const notify of state.listeners)notify({key:'soundpack:'+name,loaded});});
    if(bytes.length!==meta.bytes||hex(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)))!==meta.sha256)throw Error('Audio pack verification failed: '+name);
    const header=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);if(bytes.length<12||header.getUint32(0,true)!==0x444e5354||header.getUint32(4,true)!==1||header.getUint32(8,true)!==meta.samples)throw Error('Invalid audio pack '+name);
    return bytes;
   })();
  }
  state.listeners.add(onProgress);onProgress({key:'soundpack:'+name,loaded:state.loaded});
  try{return await state.promise;}catch(e){if(this.packs.get(name)===state)this.packs.delete(name);throw e;}finally{state.listeners.delete(onProgress);}
 }
 async sample(name,onProgress=()=>{}){
  const manifest=await this.catalog(),entry=manifest.sounds[name];if(!entry)throw Error('Unknown audio sample '+name);
  if(!this.samples.has(name))this.samples.set(name,(async()=>{
   const pack=await this.pack(entry.pack,onProgress);if(!Number.isInteger(entry.offset)||entry.offset<12||entry.offset%4||!Number.isInteger(entry.bytes)||entry.bytes<=0||entry.offset+entry.bytes>pack.length)throw Error('Invalid audio span '+name);
   const bytes=pack.slice(entry.offset,entry.offset+entry.bytes);
   if(bytes.length!==entry.bytes||hex(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)))!==entry.sha256)throw Error('Audio sample verification failed: '+name);
   // All shipped samples are22050Hz. Decode without a user gesture or an
   // output device; the resulting AudioBuffers can be used by the live context.
   this.decoder??=new OfflineAudioContext(1,1,22050);
   return this.decoder.decodeAudioData(bytes.buffer);
  })());
  try{return await this.samples.get(name);}catch(e){this.samples.delete(name);throw e;}
 }
 async preload(names,onProgress){
  let next=0;await Promise.all(Array.from({length:Math.min(6,names.length)},async()=>{while(next<names.length){const name=names[next++];await this.sample(name,onProgress);}}));
 }
 async buffer(id){
  id>>>=0;if(!this.buffers.has(id))this.buffers.set(id,(async()=>{const manifest=await this.catalog(),name=manifest.audio_resources[id];return this.sample(name);})());
  try{return await this.buffers.get(id);}catch(e){this.buffers.delete(id);throw e;}
 }
 stop(key){const voice=this.voices.get(key);if(!voice)return;this.voices.delete(key);voice.cancelled=true;if(voice.source){try{voice.source.stop();}catch{}voice.source.disconnect();voice.gain.disconnect();voice.panner?.disconnect();}}
 stopAll(keepMenu=false){this.generation++;for(const [key,voice]of this.voices){if(keepMenu&&(voice.command[3]&0x100)&&!(voice.command[3]&1))continue;this.stop(key);}}
 parameters(voice,command){
  voice.command=command;if(!voice.source)return;
  const [, , , ,gain,frequency,x,y,z]=command,now=this.context.currentTime;
  voice.gain.gain.setTargetAtTime(gain/65535,now,.01);voice.source.playbackRate.setTargetAtTime(frequency?frequency/voice.source.buffer.sampleRate:1,now,.015);
  if(voice.panner){voice.panner.positionX.value=x/1024;voice.panner.positionY.value=-y/1024;voice.panner.positionZ.value=z/1024;}
 }
 apply(commands=[],paused=false,volume=1){
  if(!this.context)return;this.master.gain.setTargetAtTime(paused?0:.35*volume,this.context.currentTime,.015);this.menuMaster.gain.setTargetAtTime(.35*volume,this.context.currentTime,.015);
  for(const command of commands){
   const [operation,key,sample,flags,,,x,y,z]=command;this.events++;
   if(operation===0){this.stop(key);continue;}
   if(operation===3){const listener=this.context.listener;listener.positionX.value=x/1024;listener.positionY.value=-y/1024;listener.positionZ.value=z/1024;continue;}
   if(operation===4){const l=this.context.listener;l.forwardX.value=x/4096;l.forwardY.value=-y/4096;l.forwardZ.value=z/4096;l.upX.value=0;l.upY.value=1;l.upZ.value=0;continue;}
   if(operation!==1&&operation!==2)continue;
   const identity=key||'once-'+this.events;let voice=this.voices.get(identity);
   if(voice&&(operation===1||voice.sample!==sample)){this.stop(identity);voice=null;}
   if(voice){this.parameters(voice,command);continue;}
   voice={sample,command,cancelled:false};this.voices.set(identity,voice);
   void this.buffer(sample).then(buffer=>{
    if(voice.cancelled||this.voices.get(identity)!==voice)return;
    const c=voice.command,source=this.context.createBufferSource(),gain=this.context.createGain();source.buffer=buffer;source.loop=!!(flags&1);voice.source=source;voice.gain=gain;source.connect(gain);
    if(flags&2){const p=this.context.createPanner();p.panningModel='equalpower';p.distanceModel='inverse';p.refDistance=8;p.maxDistance=512;p.rolloffFactor=1;gain.connect(p);p.connect(this.master);voice.panner=p;}else gain.connect(flags&0x100?this.menuMaster:this.master);
    this.parameters(voice,c);source.onended=()=>{if(this.voices.get(identity)===voice)this.stop(identity);};source.start();this.played++;
   }).catch(e=>{this.stop(identity);this.errors.push(e.message);console.error(e);});
  }
 }
}
