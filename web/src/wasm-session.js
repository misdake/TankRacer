import {decodeNativePacket} from './native-packet.js';

export class WasmSession{
 constructor({onProgress=()=>{}}={}){
  this.worker=new Worker(new URL('./wasm-worker.js',import.meta.url),{type:'module'});this.sequence=0;this.pending=new Map();
  this.worker.onmessage=({data})=>{if(data.progress){onProgress(data.progress);return;}const p=this.pending.get(data.id);if(!p)return;this.pending.delete(data.id);data.error?p.reject(Error(data.error)):p.resolve(data);};
  this.worker.onerror=e=>{for(const p of this.pending.values())p.reject(Error(e.message||'Game worker stopped'));this.pending.clear();};
 }
 send(operation,options){const id=++this.sequence;return new Promise((resolve,reject)=>{this.pending.set(id,{resolve,reject});this.worker.postMessage({id,operation,...options});});}
 decode(data){return {...decodeNativePacket(data.buffer),audio:data.audio,gain:data.gain,cup:data.cup};}
 async reset(options){return this.decode(await this.send('reset',{options}));}
 async step(options){return this.decode(await this.send('step',options));}
 async advance(options){return this.decode(await this.send('next',{options}));}
 async menuStep(bits=0,dt=16,typed=-1){return this.decode(await this.send('menu',{bits,dt,typed}));}
 async menuOpen(){return this.decode(await this.send('menu-open'));}
 async menuFinish(){return this.decode(await this.send('menu-finish'));}
 async menuUnlock(){return this.decode(await this.send('menu-unlock'));}
 unload(){return this.send('close');}
 close(){for(const p of this.pending.values())p.reject(Error('Game session ended'));this.pending.clear();this.worker.terminate();}
}
