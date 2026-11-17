// Selective verification. An explicitly selected check may never silently skip.
import {spawnSync} from 'node:child_process';
import {mkdir,readFile,readdir,stat,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.join(root,'build/rust/tests');
const references={
  camera:['reference_chase_camera.c','tankracer-game','chase_camera_reference','TANKRACER_CHASE_CAMERA_REFERENCE'],
  'vehicle-layers':['reference_vehicle_layers.c','tankracer-scene','vehicle_layers','TANKRACER_VEHICLE_LAYERS_REFERENCE'],
  'vehicle-runtime':['reference_vehicle_runtime.c','tankracer-game','vehicle_runtime_reference','TANKRACER_VEHICLE_RUNTIME_REFERENCE'],
  'scenery-runtime':['reference_scenery_runtime.c','tankracer-game','scenery_runtime_reference','TANKRACER_SCENERY_RUNTIME_REFERENCE'],
  'vehicle-model':['reference_vehicle_model.c','tankracer-game','vehicle_model_reference','TANKRACER_VEHICLE_MODEL_REFERENCE'],
  tracks:[null,'tankracer-scene','vehicle','TANKRACER_ORIGINAL_EXE'],
};
function run(command,args,env=process.env){
  const result=spawnSync(command,args,{cwd:root,env,stdio:'inherit',windowsHide:true});
  if(result.error)throw result.error;
  if(result.status!==0)throw Error(`${command} failed (${result.status})`);
}
async function requireFile(file){
  const info=await stat(file);if(!info.isFile())throw Error(`Required file is not a file: ${file}`);
}
async function hash(file){return createHash('sha256').update(await readFile(file)).digest('hex');}
async function newestSources(directory){
  let latest=0;
  for(const entry of await readdir(directory,{withFileTypes:true})){
    const file=path.join(directory,entry.name);
    if(entry.isDirectory())latest=Math.max(latest,await newestSources(file));
    else if(/\.(c|h|inc)$/.test(entry.name))latest=Math.max(latest,(await stat(file)).mtimeMs);
  }
  return latest;
}
async function nativeLibraries(cc){
  const libs=['host','core'].map(name=>path.join(root,`build/native/libtankracer_${name}.a`));
  const latest=Math.max(await newestSources(path.join(root,'src')),await newestSources(path.join(root,'host')));
  let rebuild=false;
  for(const file of libs){try{await requireFile(file);rebuild ||= (await stat(file)).mtimeMs<latest;}catch{rebuild=true;}}
  if(rebuild)run(process.env.PYTHON||'python',['tools/build.py','--target','native','--cc',cc]);
  for(const file of libs)await requireFile(file);
  return libs;
}
async function reference(names){
  if(!names.length)throw Error('Select reference modules: '+Object.keys(references).join(', '));
  for(const name of names)if(!references[name])throw Error(`Unknown reference module: ${name}`);
  await mkdir(out,{recursive:true});
  const report={requested:names,executed:[],not_selected:Object.keys(references).filter(n=>!names.includes(n)),passed:false};
  try{
    for(const name of names){
      const [source,pkg,test,variable]=references[name];
      console.log(`REFERENCE RUN: ${name}`);
      const env={...process.env};let evidence;
      if(source){
        const file=path.join(root,'tests',source);await requireFile(file);
        const cc=process.env.CC||'gcc';
        const available=spawnSync(cc,['--version'],{windowsHide:true,stdio:'ignore'});
        if(available.error||available.status!==0)throw Error('Set CC to an available GCC-compatible C compiler');
        const libs=await nativeLibraries(cc);
        const executable=path.join(root,'build/native',`rust-${name}${process.platform==='win32'?'.exe':''}`);
        run(cc,['-std=c11','-O2','-Wall','-Wextra','-Werror','-Isrc','-Ihost',file,...libs,'-lm','-o',executable]);
        await requireFile(executable);env[variable]=executable;
        evidence={source:await hash(file),libraries:await Promise.all(libs.map(hash))};
      }else{
        if(!env[variable])throw Error(`tracks requires ${variable}; no original executable is bundled`);
        await requireFile(env[variable]);evidence={original:await hash(env[variable])};
      }
      const args=['test','-p',pkg,'--test',test,'--locked'];
      if(!source)args.push('original_track_uvs_match_gpu_coefficients_and_wrapping_travel');
      args.push('--','--include-ignored','--nocapture');run('cargo',args,env);
      report.executed.push({module:name,...evidence});
    }
    report.passed=true;
  }finally{
    await writeFile(path.join(out,'references.json'),JSON.stringify(report,null,2));
    console.log('REFERENCE NOT SELECTED:',report.not_selected.join(', ')||'none');
  }
}
function unit(){run('cargo',['test','--workspace','--locked']);run(process.execPath,['tests/keys.mjs']);}
function lint(){
  run('cargo',['fmt','--all','--','--check']);
  run('cargo',['clippy','--workspace','--all-targets','--locked','--','-D','warnings']);
  for(const features of [[],['--all-features']])run('cargo',['clippy','-p','tankracer-web','--target','wasm32-unknown-unknown',...features,'--locked','--','-D','warnings']);
}
function web(){
  run('cargo',['build','-p','tankracer-game','--example','drive','--release','--locked']);
  run(process.execPath,['tools/build_rust.mjs','--test']);
  run(process.execPath,['tests/rust-browser.mjs']);
}
const [mode='check',...names]=process.argv.slice(2);
switch(mode){
  case 'reference':await reference([...new Set(names)]);break;
  case 'unit':unit();break;
  case 'lint':lint();break;
  case 'web':web();break;
  case 'check':unit();lint();web();break;
  default:throw Error('Use check, unit, lint, web, or reference <module...>');
}
if(mode!=='reference')console.log('C/ORIGINAL REFERENCES NOT RUN: select modules with node tools/test_rust.mjs reference <module...>');
