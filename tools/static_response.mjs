import {readFile,stat} from 'node:fs/promises';
import {gzip} from 'node:zlib';
import {promisify} from 'node:util';
import path from 'node:path';
const compress=promisify(gzip);
const packed=new Set(['.gz','.zip','.png','.jpg','.jpeg','.gif','.webp','.avif','.mp3','.ogg','.woff','.woff2','.mp4']);

function encodings(header=''){
 const q=new Map();
 for(const part of String(header).split(',')){
  const [name,...params]=part.trim().toLowerCase().split(';');if(!name)continue;
  const weight=params.find(p=>p.trim().startsWith('q='));const value=weight?Number(weight.trim().slice(2)):1;
  q.set(name,Number.isFinite(value)?Math.max(0,Math.min(1,value)):0);
 }
 return {gzip:q.get('gzip')??q.get('*')??0,identity:q.get('identity')??(q.get('*')===0?0:1),explicitIdentity:q.has('identity')};
}

// Cache original/compressed bytes together, deduplicate concurrent compression,
// and invalidate on file changes. Compression runs in Node's worker pool.
export function createStaticResponse({maxCacheBytes=64*1024*1024}={}){
 const cache=new Map();let used=0;
 function remove(file){const entry=cache.get(file);if(entry){used-=entry.size;cache.delete(file);}}
 function account(file,entry,bytes){if(cache.get(file)!==entry)return;entry.size+=bytes;used+=bytes;while(used>maxCacheBytes&&cache.size)remove(cache.keys().next().value);}
 return async function serve(req,res,file,type){
  const info=await stat(file);if(!info.isFile()){const e=Error('Not found');e.code='ENOENT';throw e;}
  const stamp=`${info.mtimeMs}:${info.ctimeMs}:${info.size}`;let entry=cache.get(file);
  if(entry?.stamp!==stamp){remove(file);entry={stamp,size:0,raw:null,gzip:null};cache.set(file,entry);entry.raw=readFile(file).then(bytes=>{account(file,entry,bytes.length);return bytes;}).catch(e=>{if(cache.get(file)===entry)remove(file);throw e;});}
  else{cache.delete(file);cache.set(file,entry);}
  const accept=encodings(req.headers['accept-encoding']);
  const headers={'Content-Type':type,'Cache-Control':'no-cache','Vary':'Accept-Encoding'};
  if(!accept.gzip&&!accept.identity){res.writeHead(406,headers).end();return;}
  let bytes=await entry.raw;
  if(accept.gzip>0&&(!accept.explicitIdentity||accept.gzip>=accept.identity)&&(!accept.identity||bytes.length>=1024&&!packed.has(path.extname(file).toLowerCase()))){
   entry.gzip??=compress(bytes,{level:6}).then(zipped=>{account(file,entry,zipped.length);return zipped;});
   const zipped=await entry.gzip;if(zipped.length<bytes.length||!accept.identity){bytes=zipped;headers['Content-Encoding']='gzip';}
  }
  headers['Content-Length']=bytes.length;
  res.writeHead(200,headers).end(req.method==='HEAD'?undefined:bytes);
 };
}
