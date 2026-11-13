// Renderer-owned WebGL state. Every draw supplies its complete requirements;
// only changed values reach GL. Test-side audits query GL independently.
export class GLState{
 constructor(gl){this.gl=gl;this.values=new Map();this.samplers=new Map();this.changes=0;}
 invalidate(){this.values.clear();this.samplers.clear();}
 call(key,method,...args){const old=this.values.get(key);if(old&&old.method===method&&old.args.length===args.length&&args.every((v,i)=>v===old.args[i]))return;this.gl[method](...args);this.values.set(key,{method,args});this.changes++;}
 cap(cap,on){this.call('cap:'+cap,on?'enable':'disable',cap);}
 texture(unit,object){const g=this.gl;this.textureUnit=unit;if(this.values.get('texture:'+unit)?.args[1]===object)return;this.call('activeTexture','activeTexture',g.TEXTURE0+unit);this.call('texture:'+unit,'bindTexture',g.TEXTURE_2D,object);}
 sampler(object,param,value){let values=this.samplers.get(object);if(!values){values=new Map();this.samplers.set(object,values);}if(values.get(param)===value)return;this.call('activeTexture','activeTexture',this.gl.TEXTURE0+this.textureUnit);this.gl.texParameteri(this.gl.TEXTURE_2D,param,value);values.set(param,value);this.changes++;}
 forgetTexture(object){this.samplers.delete(object);for(const [key,value]of this.values)if(key.startsWith('texture:')&&value.args[1]===object)this.values.delete(key);}
 uniform(name,location,type,...values){this.call('uniform:'+name,'uniform'+type,location,...values);}
 raster(s){
  const g=this.gl,depths=[0,g.NEVER,g.LESS,g.EQUAL,g.LEQUAL,g.GREATER,g.NOTEQUAL,g.GEQUAL,g.ALWAYS],factors=[0,g.ZERO,g.ONE,g.SRC_COLOR,g.ONE_MINUS_SRC_COLOR,g.SRC_ALPHA,g.ONE_MINUS_SRC_ALPHA,g.DST_ALPHA,g.ONE_MINUS_DST_ALPHA,g.DST_COLOR,g.ONE_MINUS_DST_COLOR,g.SRC_ALPHA_SATURATE];
  const depth=depths[s[23]||4],src=factors[s[19]],dst=factors[s[20]];if(!depth||src===undefined||dst===undefined)throw Error('Unknown depth function or blend factor');
  this.cap(g.DEPTH_TEST,!!s[7]);this.call('depthMask','depthMask',!!s[14]);this.call('depthFunc','depthFunc',depth);this.call('depthRange','depthRange',0,1);
  this.cap(g.BLEND,!!s[27]);this.call('blendEquation','blendEquation',g.FUNC_ADD);this.call('blendFunc','blendFunc',src,dst);
  const cull=s[22]||1;this.cap(g.CULL_FACE,cull!==1);this.call('cullFace','cullFace',g.BACK);this.call('frontFace','frontFace',cull===3?g.CW:g.CCW);this.cap(g.DITHER,!!s[26]);
  this.cap(g.SCISSOR_TEST,false);this.call('colorMask','colorMask',true,true,true,true);
 }
}
