// Raw WebGL TCB1 receiver. Native/Wasm owns transforms, order and materials.
import {triangleIndices} from './command-buffer.js';
import {GLState} from './gl-state.js';
import {fontAtlas} from './font-atlas.js';
import {viewportQuad} from './viewport-quad.js';
const VERTEX=`
precision highp float;
attribute vec4 aScreen;
attribute vec2 aUV;
attribute vec4 aDiffuseBytes;
attribute vec4 aSpecularBytes;
uniform vec2 uResolution;
uniform bool uPerspective;
uniform bool uDepth;
uniform bool uFontAtlas;
uniform bool uViewportQuad;
varying vec2 vUV;
varying float vRhw;
varying vec4 vDiffuse;
varying vec4 vSpecular;
void main(){
 float pixelOffset=uFontAtlas||uViewportQuad?0.:.5;
 vec2 xy=vec2((aScreen.x+pixelOffset)/uResolution.x*2.-1.,1.-(aScreen.y+pixelOffset)/uResolution.y*2.);
 gl_Position=vec4(xy,uDepth?aScreen.z*2.-1.:0.,1.);gl_PointSize=1.;
 vRhw=uPerspective?max(aScreen.w,.0000000001):1.;vUV=aUV*vRhw;
 // Little-endian ARGB in TCB1 is BGRA bytes in the vertex buffer.
 vDiffuse=aDiffuseBytes.bgra;vSpecular=aSpecularBytes.bgra;
}`;
const FRAGMENT=`
precision highp float;
uniform sampler2D uTexture;
uniform sampler2D uKeyTexture;
uniform sampler2D uKeyColorTexture;
uniform bool uTextured;
uniform bool uHasAlpha;
uniform bool uBinaryAlpha;
uniform bool uColorKey;
uniform bool uSpecular;
uniform bool uFog;
uniform vec3 uFogColor;
uniform bool uAlphaTest;
uniform int uAlphaFunc;
uniform float uAlphaRef;
uniform int uTextureBlend;
uniform bool uGroundShadow;
uniform bool uFontAtlas;
uniform vec2 uFontOffset;
uniform vec4 uFontBounds;
varying vec2 vUV;
varying float vRhw;
varying vec4 vDiffuse;
varying vec4 vSpecular;
bool alphaPass(float a){
 if(uAlphaFunc==1)return false;
 if(uAlphaFunc==2)return a<uAlphaRef;
 if(uAlphaFunc==3)return abs(a-uAlphaRef)<.001;
 if(uAlphaFunc==4)return a<=uAlphaRef;
 if(uAlphaFunc==5)return a>uAlphaRef;
 if(uAlphaFunc==6)return abs(a-uAlphaRef)>=.001;
 if(uAlphaFunc==7)return a>=uAlphaRef;
 return true;
}
void main(){
 vec2 uv=vUV/vRhw;vec4 color=vDiffuse;
 if(uFontAtlas)uv=clamp(uv+uFontOffset,uFontBounds.xy,uFontBounds.zw);
 if(uTextured){
  vec4 tex=texture2D(uTexture,uv);
  if(uColorKey&&!uHasAlpha){float coverage=texture2D(uKeyTexture,uv).r;if(coverage<.5)discard;tex.rgb=texture2D(uKeyColorTexture,uv).rgb/coverage;}
  if(uHasAlpha&&uBinaryAlpha)tex.rgb=tex.a>0.?texture2D(uKeyColorTexture,uv).rgb/tex.a:vec3(0.);
  if(!uHasAlpha)tex.a=1.;
  if(uTextureBlend==1||uTextureBlend==6)color=tex;
  else if(uTextureBlend==3)color=vec4(mix(vDiffuse.rgb,tex.rgb,tex.a),vDiffuse.a);
  else if(uTextureBlend==7)color=vec4(min(tex.rgb+vDiffuse.rgb,vec3(1.)),tex.a*vDiffuse.a);
  else if(uTextureBlend==2)color=vec4(tex.rgb*vDiffuse.rgb,uHasAlpha?tex.a:vDiffuse.a);
  else color=tex*vDiffuse;
 }
 if(uGroundShadow)color.a*=1.-smoothstep(.32,1.,length((uv-.5)*2.));
 if(uSpecular)color.rgb=min(color.rgb+vSpecular.rgb,vec3(1.));
 if(uFog)color.rgb=mix(uFogColor,color.rgb,vSpecular.a);
 if(uAlphaTest&&!alphaPass(color.a))discard;
 gl_FragColor=color;
}`;
const rgba=n=>[(n>>>16&255)/255,(n>>>8&255)/255,(n&255)/255,(n>>>24)/255];
function compile(g,type,source){const s=g.createShader(type);g.shaderSource(s,source);g.compileShader(s);if(!g.getShaderParameter(s,g.COMPILE_STATUS)){const error=g.getShaderInfoLog(s);g.deleteShader(s);throw Error(error);}return s;}
export class NativeRenderer{
 constructor(canvas){
  this.canvas=canvas;const g=this.gl=canvas.getContext('webgl',{alpha:false,antialias:false,depth:true,stencil:false,premultipliedAlpha:false,preserveDrawingBuffer:true});if(!g)throw Error('WebGL is unavailable in this browser');
  this.state=new GLState(g);this.vertexLayout=null;
  const p=this.program=g.createProgram(),vs=compile(g,g.VERTEX_SHADER,VERTEX),fs=compile(g,g.FRAGMENT_SHADER,FRAGMENT);g.attachShader(p,vs);g.attachShader(p,fs);g.linkProgram(p);g.deleteShader(vs);g.deleteShader(fs);if(!g.getProgramParameter(p,g.LINK_STATUS))throw Error(g.getProgramInfoLog(p));
  this.buffer=g.createBuffer();this.indexBuffer=g.createBuffer();this.textures=new Map();this.locations={};this.attributes={};this.wireframe=false;this.softShadows=true;
  for(const n of ['uResolution','uPerspective','uDepth','uTexture','uKeyTexture','uKeyColorTexture','uTextured','uHasAlpha','uBinaryAlpha','uColorKey','uSpecular','uFog','uFogColor','uAlphaTest','uAlphaFunc','uAlphaRef','uTextureBlend','uGroundShadow','uFontAtlas','uFontOffset','uFontBounds','uViewportQuad'])this.locations[n]=g.getUniformLocation(p,n);
  for(const n of ['aScreen','aUV','aDiffuseBytes','aSpecularBytes'])this.attributes[n]=g.getAttribLocation(p,n);
  this.topologies=[0,g.POINTS,g.LINES,g.LINE_STRIP,g.TRIANGLES,g.TRIANGLE_STRIP,g.TRIANGLE_FAN];
  g.pixelStorei(g.UNPACK_ALIGNMENT,1);g.pixelStorei(g.UNPACK_FLIP_Y_WEBGL,false);g.pixelStorei(g.UNPACK_PREMULTIPLY_ALPHA_WEBGL,false);g.pixelStorei(g.UNPACK_COLORSPACE_CONVERSION_WEBGL,g.NONE);this.fallback=this.upload(new Uint8Array([255,255,255,255]),1,1,g.RGBA);
 }
 upload(pixels,width,height,format){const g=this.gl,t=g.createTexture();this.state.texture(0,t);g.texImage2D(g.TEXTURE_2D,0,format,width,height,0,format,g.UNSIGNED_BYTE,pixels);for(const [p,v]of [[g.TEXTURE_WRAP_S,g.CLAMP_TO_EDGE],[g.TEXTURE_WRAP_T,g.CLAMP_TO_EDGE],[g.TEXTURE_MAG_FILTER,g.LINEAR],[g.TEXTURE_MIN_FILTER,g.LINEAR]])this.state.sampler(t,p,v);return t;}
 loadRGBA(updates){
  const g=this.gl;for(const {key,width,height,pixels,hasAlpha} of updates){
   if(!Number.isInteger(width)||!Number.isInteger(height)||width<=0||height<=0||pixels.byteLength!==width*height*4)throw Error('Texture size mismatch');
   const mask=new Uint8Array(width*height),filtered=pixels.slice();let binaryAlpha=true;for(let i=0;i<mask.length;i++){const a=pixels[i*4+3];mask[i]=a===0?0:255;if(!a)filtered.fill(0,i*4,i*4+3);if(a!==0&&a!==255)binaryAlpha=false;}
   const item={texture:this.upload(pixels,width,height,g.RGBA),keyTexture:this.upload(mask,width,height,g.LUMINANCE),keyColorTexture:this.upload(filtered,width,height,g.RGBA),width,height,hasAlpha:!!hasAlpha,binaryAlpha,sampler:null};this.deleteTexture(this.textures.get(key));this.textures.set(key,item);
  }const error=g.getError();if(error!==g.NO_ERROR)throw Error('WebGL texture error 0x'+error.toString(16));
 }
 async loadTextures(textures,base=globalThis.location?.href){
  // Optional packed input for offline tools. Production uploads NWF1 RGBA.
  const updates=await Promise.all(Object.entries(textures).map(async([key,meta])=>{
   const level=meta.levels?.[0];if(!level?.packed_file)throw Error('Texture pixels are missing');const response=await fetch(new URL(level.packed_file,base));if(!response.ok)throw Error('Texture request failed');const raw=await response.arrayBuffer(),{width,height}=level,fmt=level.pixel_format_words,hasAlpha=!!fmt[7];if(raw.byteLength!==width*height*2)throw Error('Texture size mismatch');
   if(!((fmt[4]===0xf800&&fmt[5]===0x7e0&&fmt[6]===0x1f&&!hasAlpha)||(fmt[4]===0xf00&&fmt[5]===0xf0&&fmt[6]===0xf&&fmt[7]===0xf000)))throw Error('Unsupported texture format');
   const v=new DataView(raw),pixels=new Uint8Array(width*height*4);for(let i=0;i<width*height;i++){const n=v.getUint16(i*2,true);pixels.set(hasAlpha?[(n>>>8&15)*17,(n>>>4&15)*17,(n&15)*17,(n>>>12)*17]:[Math.round((n>>>11&31)*255/31),Math.round((n>>>5&63)*255/63),Math.round((n&31)*255/31),n?255:0],i*4);}return {key,width,height,pixels,hasAlpha};
  }));this.loadRGBA(updates);
 }
 deleteTexture(item){if(item)for(const t of [item.texture,item.keyTexture,item.keyColorTexture]){this.gl.deleteTexture(t);this.state.forgetTexture(t);}}
 clearTextures(){for(const item of this.textures.values())this.deleteTexture(item);this.textures.clear();}
 vertexPointers(base){if(this.vertexLayout===base)return;this.vertexLayout=base;const g=this.gl;for(const [name,size,type,normalized,offset]of [['aScreen',4,g.FLOAT,false,0],['aDiffuseBytes',4,g.UNSIGNED_BYTE,true,16],['aSpecularBytes',4,g.UNSIGNED_BYTE,true,20],['aUV',2,g.FLOAT,false,24]]){const i=this.attributes[name];g.enableVertexAttribArray(i);g.vertexAttribPointer(i,size,type,normalized,32,base+offset);}}
 bindTexture(item,s){
  const g=this.gl;if(!item){for(let unit=0;unit<3;unit++)this.state.texture(unit,this.fallback);return;}
  const wrap=s[3]===1?g.REPEAT:s[3]===2?g.MIRRORED_REPEAT:g.CLAMP_TO_EDGE,mag=s[17]===1?g.NEAREST:g.LINEAR,min=s[18]===1?g.NEAREST:g.LINEAR;if(wrap!==g.CLAMP_TO_EDGE&&((item.width&(item.width-1))||(item.height&(item.height-1))))throw Error('Repeating textures require power-of-two dimensions');
  for(const [unit,t]of [item.texture,item.keyTexture,item.keyColorTexture].entries()){this.state.texture(unit,t);for(const [p,v]of [[g.TEXTURE_WRAP_S,wrap],[g.TEXTURE_WRAP_T,wrap],[g.TEXTURE_MAG_FILTER,mag],[g.TEXTURE_MIN_FILTER,min]])this.state.sampler(t,p,v);}
 }
 draw(frame){
  const packet=frame.decoded_commands;if(!packet)throw Error('Drawing commands have not been decoded');const g=this.gl,l=this.locations,{width,height,draws}=packet;this.frame=frame;
  const state=this.state;
  if(this.canvas.width!==width||this.canvas.height!==height){this.canvas.width=width;this.canvas.height=height;state.invalidate();this.vertexLayout=null;}
  state.call('framebuffer','bindFramebuffer',g.FRAMEBUFFER,null);state.call('viewport','viewport',0,0,width,height);state.cap(g.SCISSOR_TEST,false);state.call('colorMask','colorMask',true,true,true,true);state.call('depthMask','depthMask',true);state.call('clearDepth','clearDepth',1);state.call('clearColor','clearColor',...rgba(packet.clear));g.clear(g.COLOR_BUFFER_BIT|g.DEPTH_BUFFER_BIT);state.call('program','useProgram',this.program);
  // Upload the original32-byte TL arena once; no per-draw geometry/scene objects
  // or CPU float/colour repacking. Offsets also accept a view into Wasm memory.
  state.call('arrayBuffer','bindBuffer',g.ARRAY_BUFFER,this.buffer);g.bufferData(g.ARRAY_BUFFER,new Uint8Array(packet.buffer,packet.byteOffset+packet.vertexOffset,packet.vertexCount*32),g.STREAM_DRAW);this.vertexPointers(0);let vertexBase=0;state.uniform('uResolution',l.uResolution,'2f',width,height);for(const [name,unit]of [['uTexture',0],['uKeyTexture',1],['uKeyColorTexture',2]])state.uniform(name,l[name],'1i',unit);
  const seen=new Set();let triangles=0;for(const d of draws){
   const s=d.states,item=d.texture?this.textures.get(d.texture):null;if(d.texture&&!item)throw Error('Missing texture for draw: '+d.texture);
   state.raster(s);this.bindTexture(item,s);if(item)seen.add(d.texture);
   state.uniform('uViewportQuad',l.uViewportQuad,'1i',Number(viewportQuad(d,packet)));
   const font=fontAtlas(d,packet,item);state.uniform('uFontAtlas',l.uFontAtlas,'1i',Number(!!font));if(font){state.uniform('uFontOffset',l.uFontOffset,'2f',...font.offset);state.uniform('uFontBounds',l.uFontBounds,'4f',...font.bounds);}
   for(const [name,value]of [['uTextured',!!item],['uHasAlpha',!!item?.hasAlpha],['uBinaryAlpha',!!item?.binaryAlpha],['uPerspective',s[4]!==0],['uDepth',!!s[7]],['uColorKey',!!s[41]],['uTextureBlend',s[21]||2],['uSpecular',!!s[29]],['uFog',!!s[28]],['uAlphaTest',!!s[15]],['uAlphaFunc',s[25]||8],['uGroundShadow',d.presentation===1&&this.softShadows]])state.uniform(name,l[name],'1i',Number(value));
   state.uniform('uFogColor',l.uFogColor,'3f',...rgba(s[34]).slice(0,3));state.uniform('uAlphaRef',l.uAlphaRef,'1f',s[24]/255);
   if(d.primitive>=4)triangles+=d.primitive===4?Math.floor(d.count/3):Math.max(0,d.count-2);
   if(this.wireframe&&d.primitive>=4){const indices=triangleIndices(d.primitive,0,d.count),lines=new Uint16Array(indices.length*2);for(let i=0;i<indices.length;i+=3)lines.set([indices[i],indices[i+1],indices[i+1],indices[i+2],indices[i+2],indices[i]],i*2);if(vertexBase!==d.first*32){vertexBase=d.first*32;this.vertexPointers(vertexBase);}state.call('elementBuffer','bindBuffer',g.ELEMENT_ARRAY_BUFFER,this.indexBuffer);g.bufferData(g.ELEMENT_ARRAY_BUFFER,lines,g.STREAM_DRAW);g.drawElements(g.LINES,lines.length,g.UNSIGNED_SHORT,0);}
   else{if(vertexBase){vertexBase=0;this.vertexPointers(0);}g.drawArrays(this.topologies[d.primitive],d.first,d.count);}
  }const error=g.getError();if(error!==g.NO_ERROR)throw Error('WebGL error 0x'+error.toString(16));return {draws:draws.length,triangles,textures:seen.size,missingTextures:0,backend:'raw-webgl'};
 }
 dispose(){this.clearTextures();const g=this.gl;g.deleteTexture(this.fallback);g.deleteBuffer(this.buffer);g.deleteBuffer(this.indexBuffer);g.deleteProgram(this.program);}
}
