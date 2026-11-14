// End-to-end Rust/Wasm/WebGL pixel tests in a real Chromium WebGL context.
import assert from 'node:assert/strict';
import {spawn,execFileSync} from 'node:child_process';
import {existsSync} from 'node:fs';
import {mkdir,mkdtemp,readFile,writeFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createWebServer} from '../tools/serve.mjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const output=path.join(root,'build/rust/tests');await mkdir(output,{recursive:true});
const executable=process.env.CHROME||[
 'C:/Program Files/Google/Chrome/Application/chrome.exe',
 '/usr/bin/chromium','/usr/bin/chromium-browser','/usr/bin/google-chrome',
 '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
].find(existsSync);
assert.ok(executable,'Set CHROME to a Chromium executable');
const server=createWebServer(path.join(root,'build/rust/test-site'));
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const base=`http://127.0.0.1:${server.address().port}`;
const profile=await mkdtemp(path.join(output,'chrome-'));
const chrome=spawn(executable,['--headless=new','--use-angle=swiftshader','--enable-unsafe-swiftshader',
 '--no-first-run',`--user-data-dir=${profile}`,'--remote-debugging-port=0','--window-size=1280,900','about:blank'],
 {stdio:'ignore',windowsHide:true});
let socket,sequence=0;const pending=new Map(),errors=[],requests=[];
const report={passed:false};
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function wait(fn,label,timeout=30000){
 const deadline=Date.now()+timeout;
 do{const value=await fn();if(value)return value;await delay(40);}while(Date.now()<deadline);
 throw Error('Timed out: '+label);
}
function send(method,params={}){
 const id=++sequence;
 return new Promise((resolve,reject)=>{
  const timeout=setTimeout(()=>{pending.delete(id);reject(Error('CDP timed out: '+method));},30000);
  pending.set(id,{resolve:r=>{clearTimeout(timeout);resolve(r);},reject:e=>{clearTimeout(timeout);reject(e);}});
  socket.send(JSON.stringify({id,method,params}));
 });
}
async function evaluate(expression){
 const result=await send('Runtime.evaluate',{expression:`(async()=>(${expression}))()`,awaitPromise:true,returnByValue:true});
 if(result.exceptionDetails)throw Error(result.exceptionDetails.exception?.description||result.exceptionDetails.text);
 return result.result.value;
}
try{
 const targets=await wait(async()=>{
  try{
   const port=Number((await readFile(path.join(profile,'DevToolsActivePort'),'utf8')).split('\n')[0]);
   const response=await fetch(`http://127.0.0.1:${port}/json/list`);
   return response.ok?await response.json():null;
  }catch{return null;}
 },'Chromium debug endpoint');
 socket=new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);
 socket.onmessage=event=>{
  const message=JSON.parse(event.data);
  if(message.id){const p=pending.get(message.id);if(!p)return;pending.delete(message.id);message.error?p.reject(Error(message.error.message)):p.resolve(message.result);}
  else if(message.method==='Runtime.exceptionThrown')errors.push(message.params.exceptionDetails.exception?.description||message.params.exceptionDetails.text);
  else if(message.method==='Network.requestWillBeSent')requests.push(message.params.request.url);
 };
 await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
 await send('Runtime.enable');await send('Page.enable');await send('Network.enable');
 await send('Page.navigate',{url:base+'/?inspect'});
 await wait(async()=>{
  const error=await evaluate('document.body?.dataset.error');if(error)throw Error(error);
  return evaluate('document.body?.dataset.ready==="true"');
 },'Rust engine initialization');
 const initialPosition=await evaluate(`import('/main.js').then(m=>Array.from(m.app.camera_position()))`);
 await send('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyW',key:'w',windowsVirtualKeyCode:87});
 await wait(()=>evaluate(`import('/main.js').then(m=>Array.from(m.app.camera_position()).some((p,i)=>Math.abs(p-${JSON.stringify(initialPosition)}[i])>.05))`),'keyboard camera motion');
 await send('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyW',key:'w',windowsVirtualKeyCode:87});
 const initialForward=await evaluate(`import('/main.js').then(m=>Array.from(m.app.camera_forward()))`);
 const point=await evaluate('(()=>{const r=document.getElementById("game").getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()');
 await send('Input.dispatchMouseEvent',{type:'mouseMoved',...point});
 await send('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',buttons:1,clickCount:1,...point});
 await send('Input.dispatchMouseEvent',{type:'mouseMoved',buttons:1,x:point.x+40,y:point.y+15});
 await send('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',buttons:0,clickCount:1,x:point.x+40,y:point.y+15});
 const turned=await evaluate(`import('/main.js').then(m=>Array.from(m.app.camera_forward()))`);
 assert.notDeepEqual(turned,initialForward);
 await evaluate(`import('/main.js').then(m=>m.stop())`);
 const gpu=await evaluate(`import('/main.js').then(m=>JSON.parse(m.app.run_gpu_checks()))`);
 assert.equal(gpu.passed,true);assert.ok(gpu.pixel_cases>=144);assert.equal(gpu.blend_transitions,81);
 const frame=await evaluate(`import('/main.js').then(m=>{m.app.render();return JSON.parse(m.app.frame_stats());})`);
 const scene=await evaluate(`import('/main.js').then(m=>JSON.parse(m.app.scene_stats()))`);
 assert.equal(scene.source_meshes,612);assert.equal(scene.source_triangles,8881);
 assert.equal(frame.triangles,scene.rendered_triangles);assert.ok(frame.draws>0&&frame.draws<=5);
 assert.equal(scene.billboard_triangles,542);assert.ok(scene.acceleration_bytes<1_000_000);
 const before=await evaluate(`import('/main.js').then(m=>Array.from(m.app.camera_position()))`);
 await evaluate(`import('/main.js').then(m=>{m.app.move_camera(.1,1,0,1,false);m.app.look(20,-10);m.app.render();})`);
 const after=await evaluate(`import('/main.js').then(m=>Array.from(m.app.camera_position()))`);
 assert.notDeepEqual(after,before);
 const moved=await evaluate(`import('/main.js').then(m=>JSON.parse(m.app.frame_stats()))`);
 assert.ok(moved.index_upload_bytes>0);
 const stationary=await evaluate(`import('/main.js').then(m=>{m.app.render();return JSON.parse(m.app.frame_stats());})`);
 assert.equal(stationary.index_upload_bytes,0);
 await writeFile(path.join(output,'shell.png'),Buffer.from((await send('Page.captureScreenshot',{format:'png'})).data,'base64'));
 for(const [width,height]of [[1280,900],[600,900],[1600,700]]){
  await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
  const size=await evaluate('(()=>{const r=document.getElementById("game").getBoundingClientRect();return {width:r.width,height:r.height,scrollWidth:document.documentElement.scrollWidth,scrollHeight:document.documentElement.scrollHeight};})()');
  assert.ok(Math.abs(size.width/size.height-4/3)<0.002);assert.equal(size.scrollWidth,width);assert.equal(size.scrollHeight,height);
 }
 // The normal page runs the same fresh resource-only runtime inside Wasm.
 await send('Page.navigate',{url:base+'/'});
 await wait(async()=>{
  const error=await evaluate('document.body?.dataset.error');if(error)throw Error(error);
  return evaluate('document.body?.dataset.ready==="true"');
 },'Wasm vehicle initialization');
 const carStart=await evaluate(`import('/main.js').then(m=>Array.from(m.app.vehicle_position()))`);
 assert.equal(carStart.length,3);
 await send('Input.dispatchKeyEvent',{type:'keyDown',code:'ArrowUp',key:'ArrowUp',windowsVirtualKeyCode:38});
 await wait(()=>evaluate(`import('/main.js').then(m=>Math.abs(m.app.vehicle_position()[2]-${carStart[2]})>500)`),'keyboard vehicle motion');
 await send('Input.dispatchKeyEvent',{type:'keyUp',code:'ArrowUp',key:'ArrowUp',windowsVirtualKeyCode:38});
 await evaluate(`import('/main.js').then(m=>m.stop())`);
 const driven=await evaluate(`import('/main.js').then(async m=>{
   const files=await Promise.all(['DATA','BRONZE','INGAME','ENGLAND'].map(async name=>new Uint8Array(await (await fetch('./assets/'+name+'.DAT')).arrayBuffer())));
   m.app.load_player(...files);
   const begin=performance.now();
   for(let tick=0;tick<120;tick++)m.app.step_vehicle(16,65536,0,tick>=40&&tick<60?12000:0);
   const milliseconds=performance.now()-begin;
   m.app.render();m.app.check_graphics();
   return {state:Array.from(m.app.vehicle_state()),milliseconds,frame:JSON.parse(m.app.frame_stats()),scene:JSON.parse(m.app.scene_stats())};
 })`);
 report.vehicle=driven;
 const native=path.join(root,'target/release/examples/drive'+(process.platform==='win32'?'.exe':''));
 assert.ok(existsSync(native),'Build the native Rust drive example for Wasm comparison');
 const nativeOutput=execFileSync(native,[path.join(root,'assets/game'),'0','1','120','--scenery'],{encoding:'utf8',windowsHide:true,stdio:['ignore','pipe','pipe']});
 const nativeState=/end=(\[[^\]]+\]) velocity=(\[[^\]]+\]).*rng=(\d+)/.exec(nativeOutput);
 assert.ok(nativeState,nativeOutput);
 assert.deepEqual(driven.state,[...JSON.parse(nativeState[1]),...JSON.parse(nativeState[2]),Number(nativeState[3])|0,120]);
 assert.equal(driven.scene.scenery_objects,118);
 assert.ok(driven.scene.scenery_opaque_draws<driven.scene.scenery_objects);
 assert.equal(driven.frame.draws,5+driven.scene.scenery_opaque_draws+driven.scene.transparent_runs);
 assert.equal(driven.frame.triangles,driven.scene.rendered_triangles+driven.scene.scenery_triangles+12);
 assert.ok(driven.scene.acceleration_bytes<1_000_000);
 assert.equal(driven.scene.acceleration_bytes,driven.scene.render_acceleration_bytes+driven.scene.collision_acceleration_bytes);
 const still=await evaluate(`import('/main.js').then(m=>{m.app.render();return JSON.parse(m.app.frame_stats());})`);
 for(const key of ['index_upload_bytes','vertex_upload_bytes','instance_upload_bytes'])assert.equal(still[key],0,key);
 await writeFile(path.join(output,'drive.png'),Buffer.from((await send('Page.captureScreenshot',{format:'png'})).data,'base64'));
 // Reload at identical poses: new GPU buffers still require sorted index banks.
 const reloads=await evaluate(`import('/main.js').then(async m=>{
   const files=await Promise.all(['DATA','BRONZE','INGAME','ENGLAND'].map(async name=>new Uint8Array(await (await fetch('./assets/'+name+'.DAT')).arrayBuffer())));
   const results=[];
   for(let i=0;i<2;i++){m.app.load_player(...files);m.app.render();m.app.check_graphics();results.push({frame:JSON.parse(m.app.frame_stats()),scene:JSON.parse(m.app.scene_stats())});}
   return results;
 })`);
 for(const reload of reloads){assert.equal(reload.scene.scenery_objects,driven.scene.scenery_objects);assert.ok(reload.frame.index_upload_bytes>0);}
 // SR04 starts exercise Closer plus actual SWAMP Spin edge-pivoting actors.
 const collisions=[];
 for(let target=0;target<8;target++){
  const collision=await evaluate(`import('/main.js').then(async m=>{
   const files=await Promise.all(['DATA','BRONZE','INGAME',${target>=6?"'SWAMP'":"'ENGLAND'"}].map(async name=>new Uint8Array(await (await fetch('./assets/'+name+'.DAT')).arrayBuffer())));
   m.app.load_world(files[3]);m.app.load_collision(...files,${target});m.app.render();
   const uploads={vertices:0,instances:0};
   for(let tick=0;tick<12;tick++){
    m.app.step_vehicle(16,65536,0,0);m.app.render();
    const s=JSON.parse(m.app.frame_stats());uploads.vertices+=s.vertex_upload_bytes;uploads.instances+=s.instance_upload_bytes;
   }
   const state=Array.from(m.app.scenery_state());m.app.render();m.app.check_graphics();
   return {target:${target},state,uploads,stationary:JSON.parse(m.app.frame_stats()),scene:JSON.parse(m.app.scene_stats())};
  })`);
  assert.ok(collision.state[target>=6?5:target>=4?target-1:target===3?2:target===2?1:0]>0,JSON.stringify(collision));
  assert.ok(collision.uploads[target===2||target===5?'vertices':'instances']>0,JSON.stringify(collision));
  for(const key of ['index_upload_bytes','vertex_upload_bytes','instance_upload_bytes'])assert.equal(collision.stationary[key],0,key);
  assert.ok(collision.scene.acceleration_bytes<1_000_000);
  await writeFile(path.join(output,`collision-${target}.png`),Buffer.from((await send('Page.captureScreenshot',{format:'png'})).data,'base64'));
  collisions.push(collision);
 }
 assert.deepEqual(errors,[]);assert.equal(requests.filter(url=>new URL(url).pathname.startsWith('/api/')).length,0);
 assert.ok(requests.some(url=>url.endsWith('/pkg/tankracer_bg.wasm')));
 Object.assign(report,{passed:true,...gpu,frame,scene,free_camera:true,keyboard_motion:true,pointer_look:true,stationary_upload_bytes:0,aspect_ratios:3,rust_wasm:true,raw_webgl2:true,vehicle:driven,native_wasm_updates:120,keyboard_driving:true,collisions});
 console.log('RUST BROWSER: PASS',report);
}catch(error){report.error=error.stack;throw error;}
finally{
 await writeFile(path.join(output,'browser.json'),JSON.stringify(report,null,2));
 socket?.close();chrome.kill();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
