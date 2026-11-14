// Rust toolchain + wasm-pack generate the browser ABI. No C or Python involved.
import {spawnSync} from 'node:child_process';
import {cp,copyFile,mkdir,stat} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const testing=process.argv.includes('--test');
const output=path.join(root,'build/rust',testing?'test-site':'site');
function run(command,args){
 const result=spawnSync(command,args,{cwd:root,stdio:'inherit',windowsHide:true});
 if(result.error)throw result.error;
 if(result.status!==0)throw Error(`${command} failed (${result.status})`);
}
await mkdir(output,{recursive:true});
const args=['build','crates/web','--target','web','--release','--out-dir',path.join(output,'pkg'),'--out-name','tankracer','--no-typescript'];
if(testing)args.push('--features','gpu-tests');
args.push('--locked');
run('wasm-pack',args);
await cp(path.join(root,'browser'),output,{recursive:true});
await mkdir(path.join(output,'assets'),{recursive:true});
for(const [source,name]of [
 ['LEVELS/ENGLAND','ENGLAND'],['DATA','DATA'],['TANKS/BRONZE','BRONZE'],['INGAME','INGAME'],
]) await copyFile(path.join(root,'assets/game',source,'FASTFILE.DAT'),path.join(output,'assets',`${name}.DAT`));
console.log(`Rust site: ${output}`);
console.log(`Wasm: ${(await stat(path.join(output,'pkg/tankracer_bg.wasm'))).size} bytes`);
console.log(`Serve: node tools/serve.mjs 8133 build/rust/${testing?'test-site':'site'}`);
