// Exercise the actual gzip server and verify every shipped immutable resource.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFile,mkdir,writeFile,readdir} from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {createWebServer} from '../tools/serve.mjs';
const root=path.resolve('dist'),manifest=JSON.parse(await readFile(path.join(root,'wasm/manifest.json'),'utf8'));
const sha=b=>createHash('sha256').update(b).digest('hex');
const server=createWebServer(root);await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
function request(url,method='GET',encoding='gzip'){
 return new Promise((resolve,reject)=>{
  const req=http.request({host:'127.0.0.1',port:server.address().port,path:url,method,headers:{'Accept-Encoding':encoding}},res=>{
   const chunks=[];res.on('data',b=>chunks.push(b));res.on('end',()=>resolve({status:res.statusCode,headers:res.headers,body:Buffer.concat(chunks)}));res.on('error',reject);
  });req.on('error',reject);req.end();
 });
}
async function files(folder){const found=[];for(const entry of await readdir(folder,{withFileTypes:true})){const name=path.join(folder,entry.name);found.push(...entry.isDirectory()?await files(name):[name]);}return found;}
const report={passed:false,files:0,gzip_files:0,archives:0,audio_samples:0};
try{
 for(const file of await files(root)){
  const url='/'+path.relative(root,file).split(path.sep).join('/'),bytes=await readFile(file),response=await request(url);
  assert.equal(response.status,200,url);assert.equal(Number(response.headers['content-length']),response.body.length,url);
  const gzip=response.headers['content-encoding']==='gzip';assert.deepEqual(gzip?gunzipSync(response.body):response.body,bytes,url);
  const head=await request(url,'HEAD');assert.equal(head.status,200);assert.equal(head.body.length,0);assert.equal(head.headers['content-length'],response.headers['content-length']);
  if(gzip)report.gzip_files++;report.files++;
 }
 for(const [name,entry]of Object.entries({...manifest.assets,...manifest.sound_packs,...Object.fromEntries(Object.entries(manifest.files).map(([name,e])=>[name,{...e,path:name}]))})){
  const bytes=await readFile(path.join(root,'wasm',entry.path));assert.equal(bytes.length,entry.bytes,name);assert.equal(sha(bytes),entry.sha256,name);
 }
 report.archives=Object.keys(manifest.assets).length;assert.equal(report.archives,15);
 for(const [name,entry]of Object.entries(manifest.sounds)){
  const pack=await readFile(path.join(root,'wasm',manifest.sound_packs[entry.pack].path));
  assert.equal(pack.toString('ascii',0,4),'TSND');assert.equal(pack.readUInt32LE(4),1);
  const sample=pack.subarray(entry.offset,entry.offset+entry.bytes);assert.equal(sample.length,entry.bytes,name);assert.equal(sha(sample),entry.sha256,name);
  assert.equal(sample.toString('ascii',0,4),'RIFF');assert.equal(sample.toString('ascii',8,12),'WAVE');report.audio_samples++;
 }
 assert.equal(report.audio_samples,117);
 const identity=await request('/wasm/tankracer.wasm','GET','identity');assert.equal(identity.headers['content-encoding'],undefined);assert.equal(identity.headers['content-type'],'application/wasm');
 for(const url of ['/api/state','/does-not-exist'])assert.equal((await request(url)).status,404);
 assert.equal((await request('/','POST')).status,404);assert.equal((await request('/','GET','identity;q=0,gzip;q=0')).status,406);
 report.passed=true;console.log(`SITE: PASS ${report.files} files, ${report.gzip_files} gzip responses, 15 archives, 117 audio samples`);
}finally{
 await new Promise((resolve,reject)=>server.close(e=>e?reject(e):resolve()));await mkdir('build/tests',{recursive:true});await writeFile('build/tests/site.json',JSON.stringify(report,null,2));
}
