import {decodeCommands} from './command-buffer.js';
export const MAX_PACKET_BYTES=64*1024*1024;
// NWF1 wraps the existing TCB1 stream and newly-created RGBA resources. Native
// process pointers never cross as pointers: texture IDs are opaque tokens.
export function decodeNativePacket(buffer,offset=0,length=buffer.byteLength-offset){
 const v=new DataView(buffer,offset,length),word=at=>v.getUint32(at,true);
 if(length<20||length>MAX_PACKET_BYTES||word(0)!==0x3146574e||word(4)!==length)throw Error('Invalid game frame');
 const jsonBytes=word(8),commandBytes=word(12),textureCount=word(16);
 if(jsonBytes>65536||20+jsonBytes+commandBytes>length||textureCount>4096)throw Error('Invalid frame bounds');
 const metadata=JSON.parse(new TextDecoder().decode(new Uint8Array(buffer,offset+20,jsonBytes)));
 if(!Number.isInteger(metadata.frame))throw Error('Invalid frame state');
 if(metadata.frontend){if(!Number.isInteger(metadata.page)||metadata.page<0||metadata.page>=61||!Number.isInteger(metadata.phase))throw Error('Invalid menu state');}
 else if(!Array.isArray(metadata.position)||metadata.position.length!==3||metadata.position.some(n=>!Number.isFinite(n)))throw Error('Invalid frame state');
 const commands=decodeCommands(buffer,offset+20+jsonBytes,commandBytes);
 if(commands.sequence!==metadata.frame)throw Error('Frame sequence mismatch');
 let at=20+jsonBytes+commandBytes;const textures=[];
 for(let i=0;i<textureCount;i++){
  if(at+16>length)throw Error('Truncated texture header');
  const token=word(at),width=word(at+4),height=word(at+8),bytes=word(at+12);at+=16;
  if(!token||!width||!height||width>4096||height>4096||bytes!==width*height*4||at+bytes>length)throw Error('Invalid texture bounds');
  if(metadata.texture_alpha&&![0,1].includes(metadata.texture_alpha[token]))throw Error('Missing texture alpha type');
  textures.push({key:'0x'+token.toString(16),width,height,hasAlpha:metadata.texture_alpha?!!metadata.texture_alpha[token]:true,pixels:new Uint8Array(buffer,offset+at,bytes)});at+=bytes;
 }
 if(at!==length)throw Error('Unexpected frame data');
 return {metadata,commands,textures};
}
