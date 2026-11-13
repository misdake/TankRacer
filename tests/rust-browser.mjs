// End-to-end Rust/Wasm/WebGL pixel tests in a real Chromium WebGL context.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {existsSync} from 'node:fs';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
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
const profile=path.join(output,'chrome-'+process.pid);
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
 const port=await wait(async()=>{
  try{return Number((await readFile(path.join(profile,'DevToolsActivePort'),'utf8')).split('\n')[0]);}catch{return null;}
 },'Chromium debug port');
 const targets=await(await fetch(`http://127.0.0.1:${port}/json/list`)).json();
 socket=new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);
 socket.onmessage=event=>{
  const message=JSON.parse(event.data);
  if(message.id){const p=pending.get(message.id);if(!p)return;pending.delete(message.id);message.error?p.reject(Error(message.error.message)):p.resolve(message.result);}
  else if(message.method==='Runtime.exceptionThrown')errors.push(message.params.exceptionDetails.exception?.description||message.params.exceptionDetails.text);
  else if(message.method==='Network.requestWillBeSent')requests.push(message.params.request.url);
 };
 await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
 await send('Runtime.enable');await send('Page.enable');await send('Network.enable');
 await send('Page.navigate',{url:base+'/'});
 await wait(async()=>{
  const error=await evaluate('document.body?.dataset.error');if(error)throw Error(error);
  return evaluate('document.body?.dataset.ready==="true"');
 },'Rust engine initialization');
 await evaluate(`import('/main.js').then(m=>m.stop())`);
 const gpu=await evaluate(`import('/main.js').then(m=>JSON.parse(m.app.run_gpu_checks()))`);
 assert.equal(gpu.passed,true);assert.ok(gpu.pixel_cases>=100);assert.equal(gpu.blend_transitions,81);
 const frame=await evaluate(`import('/main.js').then(m=>{m.app.render(0);return JSON.parse(m.app.frame_stats());})`);
 assert.equal(frame.draws,4);assert.equal(frame.triangles,48);
 await writeFile(path.join(output,'shell.png'),Buffer.from((await send('Page.captureScreenshot',{format:'png'})).data,'base64'));
 for(const [width,height]of [[1280,900],[600,900],[1600,700]]){
  await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
  const size=await evaluate('(()=>{const r=document.getElementById("game").getBoundingClientRect();return {width:r.width,height:r.height,scrollWidth:document.documentElement.scrollWidth,scrollHeight:document.documentElement.scrollHeight};})()');
  assert.ok(Math.abs(size.width/size.height-4/3)<0.002);assert.equal(size.scrollWidth,width);assert.equal(size.scrollHeight,height);
 }
 assert.deepEqual(errors,[]);assert.equal(requests.filter(url=>new URL(url).pathname.startsWith('/api/')).length,0);
 assert.ok(requests.some(url=>url.endsWith('/pkg/tankracer_bg.wasm')));
 Object.assign(report,{passed:true,...gpu,frame,aspect_ratios:3,rust_wasm:true,raw_webgl2:true});
 console.log('RUST BROWSER: PASS',report);
}catch(error){report.error=error.stack;throw error;}
finally{
 await writeFile(path.join(output,'browser.json'),JSON.stringify(report,null,2));
 socket?.close();chrome.kill();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
