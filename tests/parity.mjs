import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import path from 'node:path';
import {WasmEngine,maps} from '../dist/src/wasm-engine.js';
import {decodeNativePacket} from '../dist/src/native-packet.js';
const root=path.resolve('dist/wasm'),out=path.resolve('build/tests');await mkdir(out,{recursive:true});
const manifest=JSON.parse(await readFile(path.join(root,'manifest.json'),'utf8'));
const engine=await WasmEngine.create({manifest,readAsset:e=>readFile(path.join(root,e.path))});
const report={passed:false,states:0,game_updates:0,full_DATA_bytes_per_state:0x800000,maps:[],command_buffers_exact:true,texture_bytes_exact:true};
const stable=m=>Object.fromEntries(Object.entries(m).filter(([key])=>!['simulation_ms','command_ms'].includes(key)));
function reference(options,actions){
 const args=['--assets',path.resolve('assets/game'),'--map',options.map,'--cup',options.cup,'--vehicle',String(options.vehicle??0),'--mode',String(options.mode??0),'--snapshot'];
 if(options.opponents===false)args.push('--no-opponents');
 const p=spawn(path.resolve(process.env.TANKRACER_NATIVE || ('build/native/tankracer'+(process.platform==='win32'?'.exe':''))),args,{windowsHide:true});
 let buffer=Buffer.alloc(0),failure='',ended=false;const waiting=[],packets=[];
 const deliver=()=>{while(buffer.length>=8){const n=buffer.readUInt32LE(0),ram=buffer.readUInt32LE(4);assert.equal(ram,0x800000);if(buffer.length<8+n+ram)return;const item={packet:buffer.subarray(8,8+n),ram:buffer.subarray(8+n,8+n+ram)};buffer=buffer.subarray(8+n+ram);waiting.length?waiting.shift().resolve(item):packets.push(item);}};
 p.stdout.on('data',b=>{buffer=Buffer.concat([buffer,b]);deliver();});p.stderr.on('data',b=>failure+=b);p.on('exit',code=>{ended=true;if(code!==0)for(const w of waiting)w.reject(Error('native reference '+code+': '+failure));});
 p.stdin.end(actions.map(([n,b,f=true])=>`${n} ${b.toString(16)} ${Number(f)}\n`).join(''));
 return {next:()=>packets.length?Promise.resolve(packets.shift()):ended?Promise.reject(Error('reference ended: '+failure)):new Promise((resolve,reject)=>waiting.push({resolve,reject})),close:()=>p.kill()};
}
try{
 for(const map of maps){
  const cup=map==='CIRCUIT'?'G':['EAST','BIO','MONACO'].includes(map)?'S':'B';const options={map,cup};
  const actions=map==='ENGLAND'?[[0,0],[1,0],...Array.from({length:26},()=>[8,1]),[8,9],[8,5],[1,16],[2,8193],[2,4097],[1,16385],[1,32769],[1,65537],[1,131072],[1,0],[1,16384],[1,0],[1,32768],[2,32768],[1,0],[1,16384],[1,0],[1,32768],[1,0],[1,16384],[1,0],[1,32768],[1,0],[0,262143,false],[1,32],[2,32],[1,0],[1,32],[1,0],[1,1024],[0,0],[1,0]]:[[0,0],[8,1],[8,9]];
  const native=reference(options,actions);
  try{
   let current=await engine.reset(options);
   for(let i=0;i<actions.length;i++){
    const [n,b,f=true]=actions[i];if(i)current=engine.step(n,b,f);
    const expected=await native.next(),actual=decodeNativePacket(current),other=decodeNativePacket(expected.packet.buffer,expected.packet.byteOffset,expected.packet.length);
    assert.deepEqual(stable(actual.metadata),stable(other.metadata),`${map} action${i} metadata`);
    assert.deepEqual(Buffer.from(actual.commands.buffer,actual.commands.byteOffset,actual.commands.byteLength),Buffer.from(other.commands.buffer,other.commands.byteOffset,other.commands.byteLength),`${map} action${i} TCB1`);
    assert.equal(actual.textures.length,other.textures.length);
    for(let k=0;k<actual.textures.length;k++)assert.deepEqual(Buffer.from(actual.textures[k].pixels),Buffer.from(other.textures[k].pixels),`${map} texture${k}`);
    const ram=Buffer.from(engine.snapshot());const equal=ram.equals(expected.ram);
    if(!equal){let at=0;while(ram[at]===expected.ram[at])at++;throw Error(`${map} action${i} DATA differs at ${at.toString(16)}: wasm${ram[at]} native${expected.ram[at]}`);}
    report.states++;report.game_updates+=n;
   }
   report.maps.push(map);console.log('WASM PARITY:',map,actions.length,'full DATA/TCB1/texture states');
  }finally{native.close();}
 }
 assert.throws(()=>engine.step(9,0));assert.throws(()=>engine.step(1,-1));report.passed=true;
}finally{engine.close();await writeFile(path.join(out,'parity.json'),JSON.stringify(report,null,2));}
console.log('WASM PARITY: PASS',report.states,'states,',report.game_updates,'updates; 8 maps, full8MiB DATA, exact TCB1 and texture bytes');
