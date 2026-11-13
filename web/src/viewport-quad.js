// Full viewport overlays need edges at WebGL pixel boundaries. Retain the
// legacy D3D sample convention for ordinary scene geometry and sprites.
export function viewportQuad(draw,packet){
 if(draw.texture||draw.states[7]||draw.states[4]!==0||draw.count!==4||draw.primitive<4)return false;
 const v=new DataView(packet.buffer,packet.byteOffset+packet.vertexOffset+draw.first*32,128);
 let corners=0;
 for(let i=0;i<4;i++){
  const x=v.getFloat32(i*32,true),y=v.getFloat32(i*32+4,true);
  if(x===0&&y===0)corners|=1;
  else if(x===packet.width&&y===0)corners|=2;
  else if(x===packet.width&&y===packet.height)corners|=4;
  else if(x===0&&y===packet.height)corners|=8;
 }
 return corners===15;
}
