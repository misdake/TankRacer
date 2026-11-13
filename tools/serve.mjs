// Static files only: no native process, game API, Python or emulator.
import http from 'node:http';
import {createStaticResponse} from './static_response.mjs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const defaultRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../dist');
const mime={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.mjs':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json','.wasm':'application/wasm','.wav':'audio/wav','.png':'image/png'};
export function createWebServer(directory=defaultRoot){const root=path.resolve(directory),serve=createStaticResponse();return http.createServer(async(req,res)=>{
 try{
  const url=new URL(req.url,'http://localhost');
  if(!['GET','HEAD'].includes(req.method)||url.pathname.startsWith('/api/')){res.writeHead(404).end();return;}
  const file=path.resolve(root,'.'+decodeURIComponent(url.pathname==='/'?'/index.html':url.pathname));
  if(!file.startsWith(root+path.sep)){res.writeHead(403).end();return;}
  await serve(req,res,file,mime[path.extname(file)]||'application/octet-stream');
 }catch(e){res.writeHead(e.code==='ENOENT'?404:400).end(e.message);}
 });}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const server=createWebServer(process.argv[3]);server.listen(Number(process.argv[2]||8124),'127.0.0.1',()=>console.log(`Static Wasm → TCB1 → WebGL: http://127.0.0.1:${server.address().port}`));
}
