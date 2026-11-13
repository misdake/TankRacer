"""Build from this checkout only. Python is a build tool, never a game runtime."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MODULES = json.loads((ROOT/'modules.json').read_text())
GAME = [ROOT/'src'/f'{module}.c' for module in MODULES]
FLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'src'), '-I'+str(ROOT/'host')]
API = ['create','mount','start','step','packet','packet_size','data','destroy','advance',
       'audio','audio_count','menu_create','menu_open','menu_finish','menu_unlock',
       'menu_step','menu_packet','menu_packet_size','menu_data','menu_audio','menu_audio_count','audio_gain']

def run(command, **kwargs):
    subprocess.run(list(map(str, command)), check=True, **kwargs)

def info(file):
    return {'bytes': file.stat().st_size, 'sha256': hashlib.sha256(file.read_bytes()).hexdigest()}

def native(args):
    compiler = shutil.which(args.cc)
    if not compiler:
        raise SystemExit('C compiler not found. Supply --cc /path/to/gcc or set CC.')
    archive_tool = shutil.which(args.ar) if args.ar else None
    if not archive_tool:
        archive_tool = next((str(p) for p in (Path(compiler).with_name('ar.exe'), Path(compiler).with_name('ar')) if p.is_file()), None) or shutil.which('ar')
    if not archive_tool:
        raise SystemExit('Archive tool not found. Supply --ar /path/to/ar.')
    out = ROOT/'build/native'
    out.mkdir(parents=True, exist_ok=True)
    def compile(source):
        obj = out/(source.stem+'.o')
        result = subprocess.run([compiler,*FLAGS,'-c',str(source),'-o',str(obj)],capture_output=True,text=True)
        if result.returncode:
            raise RuntimeError(f'{source.name}:\n{result.stdout}{result.stderr}')
        return obj
    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        objects = list(workers.map(compile, [*GAME, ROOT/'host/game_host.c']))
    core, host = out/'libtankracer_core.a', out/'libtankracer_host.a'
    # Recreate archives so removed modules cannot survive an incremental build.
    core.unlink(missing_ok=True)
    host.unlink(missing_ok=True)
    run([archive_tool,'rcs',core,*objects[:-1]])
    run([archive_tool,'rcs',host,objects[-1]])
    executable = out/('tankracer.exe' if os.name=='nt' else 'tankracer')
    run([compiler,*FLAGS,ROOT/'host/native_main.c',host,core,'-o',executable])
    print('Native:', executable)

def wasm(args):
    if not args.emsdk:
        raise SystemExit('Supply --emsdk /path/to/emsdk or set EMSDK (tested with 7.0.0).')
    sdk = Path(args.emsdk).expanduser().resolve()
    compiler = sdk/'upstream/emscripten/emcc.py'
    if not compiler.is_file():
        raise SystemExit(f'Emscripten compiler not found: {compiler}')
    env = os.environ.copy()
    env['EM_CONFIG'] = str(sdk/'.emscripten')
    dist = ROOT/'dist'
    # Build to staging first; a compiler failure leaves the previous site intact.
    staging = ROOT/'build/site'
    if staging.exists():
        if staging.resolve().parent != (ROOT/'build').resolve():
            raise SystemExit('Invalid staging path')
        shutil.rmtree(staging)
    shutil.copytree(ROOT/'web', staging)
    out = staging/'wasm'
    out.mkdir()
    flags = [*FLAGS,'--no-entry','-sMODULARIZE=1','-sEXPORT_ES6=1','-sENVIRONMENT=web,worker,node',
             '-sALLOW_MEMORY_GROWTH=1','-sINITIAL_MEMORY=134217728','-sMAXIMUM_MEMORY=536870912',
             '-sSTACK_SIZE=2097152','-sFILESYSTEM=0','-sASSERTIONS=1',
             '-sEXPORTED_RUNTIME_METHODS=["HEAPU8"]',
             '-sEXPORTED_FUNCTIONS='+json.dumps(['_malloc','_free',*('_tank_'+name for name in API)]),
             '-sINCOMING_MODULE_JS_API=["wasmBinary","locateFile","print","printErr","onAbort","onRuntimeInitialized"]']
    run([sys.executable,compiler,*flags,ROOT/'host/game_host.c',*GAME,'-o',out/'tankracer.mjs'],env=env)
    manifest = dict(format='tankracer.wasm.v1',
                    compiler=subprocess.check_output([sys.executable,str(compiler),'--version'],env=env,text=True).splitlines()[0],
                    sources={}, assets={}, files={}, fixed_step_ms=16, game_modules=len(MODULES),
                    runtime_dependencies=['browser WebAssembly','Web Worker','WebGL'], original_code_or_snapshot_required=False)
    for source in sorted([*(ROOT/'src').glob('*'),*(ROOT/'host').glob('*')]):
        if source.is_file():
            manifest['sources'][source.relative_to(ROOT).as_posix()] = info(source)['sha256']
    for source in sorted((ROOT/'assets/game').rglob('FASTFILE.DAT')):
        relative = source.relative_to(ROOT/'assets/game')
        target = out/'assets'/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        manifest['assets'][relative.parent.as_posix()] = {'path': target.relative_to(out).as_posix(), **info(target)}
    manifest.update(json.loads((ROOT/'assets/audio.json').read_text()))
    for entry in manifest['sound_packs'].values():
        source = ROOT/'assets'/entry['path']
        assert info(source) == {key: entry[key] for key in ('bytes','sha256')}, source
        target = out/entry['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    manifest['files'] = {name: info(out/name) for name in ('tankracer.mjs','tankracer.wasm')}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    # All output paths are fixed children of this checkout.
    if dist.exists():
        if dist.resolve().parent != ROOT:
            raise SystemExit('Invalid site output path')
        shutil.rmtree(dist)
    shutil.copytree(staging, dist)
    shutil.rmtree(staging)
    print('Static site:', dist, '; Wasm:', manifest['files']['tankracer.wasm']['bytes'], 'bytes')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['native','wasm','all'], default='all')
    parser.add_argument('--cc', default=os.environ.get('CC','gcc'))
    parser.add_argument('--ar', default=os.environ.get('AR'))
    parser.add_argument('--emsdk', default=os.environ.get('EMSDK'))
    parser.add_argument('--jobs', type=int, default=min(8,os.cpu_count() or 1))
    parser.add_argument('--package', action='store_true', help='ZIP the generated static site')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.target in ('native','all'):
        native(args)
    if args.target in ('wasm','all'):
        wasm(args)
    if args.package:
        if not (ROOT/'dist/wasm/manifest.json').is_file():
            raise SystemExit('Build the Wasm site before packaging.')
        target = ROOT/'build/TankRacerCUP-web.zip'
        with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
            for file in sorted((ROOT/'dist').rglob('*')):
                if file.is_file():
                    archive.write(file,file.relative_to(ROOT/'dist').as_posix())
        print('Package:', target, ';',target.stat().st_size,'bytes')

if __name__ == '__main__':
    main()
