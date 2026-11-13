import {WasmEngine} from './wasm-engine.js';
let engine,queue=Promise.resolve();
self.onmessage=({data})=>{
 queue=queue.then(async()=>{
  try{
   engine??=await WasmEngine.create({onProgress:progress=>self.postMessage({id:data.id,progress})});let buffer;
   if(data.operation==='menu')buffer=await engine.menuStep(data.bits,data.dt,data.typed);
   else if(data.operation==='menu-open')buffer=await engine.menuOpen();
   else if(data.operation==='menu-finish')buffer=await engine.menuFinish();
   else if(data.operation==='menu-unlock')buffer=await engine.menuUnlock();
   else if(data.operation==='reset')buffer=await engine.reset(data.options);
   else if(data.operation==='next')buffer=await engine.advance(data.options);
   else if(data.operation==='step')buffer=engine.step(data.count,data.bits,data.focused);
   else if(data.operation==='close'){engine.close();self.postMessage({id:data.id});return;}
   else throw Error('Unknown Wasm request');
   const menu=data.operation.startsWith('menu');self.postMessage({id:data.id,buffer,audio:engine.audioCommands(menu),gain:menu?1:engine.audioGain(),cup:menu?null:engine.cupStatus()},[buffer]);
  }catch(e){self.postMessage({id:data.id,error:e.message});}
 });
};
