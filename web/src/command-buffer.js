// TCB1 is shared by native C and the future Wasm producer. All offsets are LE.
export const HEADER_BYTES=48, DRAW_BYTES=192, VERTEX_BYTES=32;

export function decodeCommands(buffer,byteOffset=0,byteLength=buffer.byteLength-byteOffset){
 const view=new DataView(buffer,byteOffset,byteLength);
 if(byteLength<HEADER_BYTES || view.getUint32(0,true)!==0x31424354 || view.getUint32(4,true)!==1)throw Error('Invalid drawing commands');
 const word=offset=>view.getUint32(offset,true),size=word(8),width=word(12),height=word(16),sequence=word(20),drawCount=word(24),vertexCount=word(28),drawOffset=word(32),vertexOffset=word(36);
 if(size!==byteLength || !width || !height || drawOffset!==HEADER_BYTES || vertexOffset!==HEADER_BYTES+drawCount*DRAW_BYTES || vertexOffset+vertexCount*VERTEX_BYTES!==size || word(44)!==0)throw Error('Drawing command bounds mismatch');
 const draws=[];
 for(let i=0;i<drawCount;i++){
  const at=drawOffset+i*DRAW_BYTES,primitive=word(at),texture=word(at+4),first=word(at+8),count=word(at+12),presentation=word(at+20);
  if(primitive<1 || primitive>6 || !count || count>4096 || first+count>vertexCount || presentation>1 || word(at+188)!==0)throw Error('Invalid draw bounds');
  const states={};for(let s=1;s<=41;s++)states[s]=word(at+24+(s-1)*4);
  draws.push({primitive,texture:texture?'0x'+texture.toString(16):null,first,count,flags:word(at+16),presentation,states});
 }
 return {buffer,view,byteOffset,byteLength,width,height,sequence,draws,vertexCount,vertexOffset,clear:word(40)};
}

export async function loadCommands(frame,base=globalThis.location?.href){
 const packet=frame.command_buffer;
 if(!packet || packet.format!=='TCB1' || packet.version!==1)throw Error('Missing drawing commands');
 let buffer;
 if(packet.encoding==='base64'){
  const raw=atob(packet.data),bytes=new Uint8Array(raw.length);for(let i=0;i<raw.length;i++)bytes[i]=raw.charCodeAt(i);buffer=bytes.buffer;
 }else if(packet.encoding==='url'){
  const response=await fetch(new URL(packet.url,base));if(!response.ok)throw Error('Unable to load drawing commands');buffer=await response.arrayBuffer();
 }else throw Error('Unsupported drawing command encoding');
 if(buffer.byteLength!==packet.byte_length)throw Error('Invalid drawing command size');
 const result=decodeCommands(buffer);
 if(result.draws.length!==packet.draw_count || result.vertexCount!==packet.vertex_count)throw Error('Invalid drawing command count');
 return result;
}

export function triangleIndices(primitive,first,count){
 const out=[];
 if(primitive===4){for(let i=0;i+2<count;i+=3)out.push(first+i,first+i+1,first+i+2);}
 else if(primitive===5){for(let i=0;i+2<count;i++)out.push(first+i+(i&1),first+i+1-(i&1),first+i+2);}
 else if(primitive===6){for(let i=1;i+1<count;i++)out.push(first,first+i,first+i+1);}
 return out;
}

// The captured flat gray footprint becomes a soft presentation decal. Original
// geometry, order and opacity remain inputs. This is not a new physical shadow.
export function groundShadowAlpha(u,v){
 const x=(u-.5)*2,y=(v-.5)*2,r=Math.sqrt(x*x+y*y);
 const t=Math.max(0,Math.min(1,(r-.32)/.68));return 1-t*t*(3-2*t);
}
