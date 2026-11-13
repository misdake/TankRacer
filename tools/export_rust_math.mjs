// One-time import of the qualified mathematical constants, never captured RAM.
// Production Rust builds use the committed compact tables and do not need C/Node.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
const root=new URL('../',import.meta.url);
const source=await readFile(new URL('src/frame_math_tables.h',root),'utf8');
await mkdir(new URL('crates/game/data/',root),{recursive:true});
for(const [name,length,file] of [['math_sine',20480,'sine.bin'],['math_angle',257,'angle.bin']]) {
 const match=source.match(new RegExp(`static const int32_t ${name}\\[${length}\\]=\\{([\\s\\S]*?)\\};`));
 if(!match) throw new Error(`Missing ${name}[${length}] in verified source`);
 const values=match[1].split(',').map(s=>s.trim()).filter(Boolean).map(Number);
 if(values.length!==length||values.some(v=>!Number.isInteger(v)||v<-32768||v>32767)) throw new Error(`Invalid ${name}`);
 const bytes=Buffer.alloc(length*2);values.forEach((v,i)=>bytes.writeInt16LE(v,i*2));
 await writeFile(new URL(`crates/game/data/${file}`,root),bytes);
 console.log(`${name}: ${length} exact values, ${bytes.length} bytes`);
}
