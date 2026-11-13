// Keep the original C UVs in TCB1. Adapt font sampling to WebGL texel centers
// and confine bilinear filtering to this glyph, rather than the whole atlas.
export function fontAtlas(draw,packet,texture){
 const hud=draw.texture>=0xf1000001&&draw.texture<=0xf1000003;
 const menu=draw.texture>=0xf2000007&&draw.texture<=0xf2000009;
 if(!texture||!hud&&!menu||draw.count!==4)return null;
 const v=new DataView(packet.buffer,packet.byteOffset+packet.vertexOffset+draw.first*32,128);
 let u0=Infinity,v0=Infinity,u1=-Infinity,v1=-Infinity;
 for(let i=0;i<4;i++){const u=v.getFloat32(i*32+24,true),y=v.getFloat32(i*32+28,true);u0=Math.min(u0,u);u1=Math.max(u1,u);v0=Math.min(v0,y);v1=Math.max(v1,y);}
 //GFONT2B also stores the yellow header ribbon; only its glyph quads opt in.
 if(menu&&((u1-u0)*texture.width>16.001||(v1-v0)*texture.height>16.001))return null;
 //50E530 adds32/65536 to ordinary menu UVs. It is not a WebGL half texel.
 const legacy=menu?1/2048:0,du=.5/texture.width,dv=.5/texture.height;
 return {offset:[-legacy,-legacy],bounds:[u0+du-legacy,v0+dv-legacy,u1-du-legacy,v1-dv-legacy]};
}
