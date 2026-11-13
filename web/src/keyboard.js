const driving={ArrowUp:1,ArrowDown:2,ArrowLeft:4,ArrowRight:8,Space:16,Enter:64,KeyZ:4096,KeyX:8192,KeyV:16384,KeyC:32768,KeyA:65536,KeyH:131072};
export class GameKeyboard{
 constructor(){this.held=new Set();this.pressed=new Set();}
 down(code){if(!(code in driving))return false;if(!this.held.has(code))this.pressed.add(code);this.held.add(code);return true;}
 up(code){this.held.delete(code);return code in driving;}
 clear(){this.held.clear();this.pressed.clear();}
 sample(paused=false){
  let bits=0;for(const code of new Set([...this.held,...this.pressed])){
   bits|=paused&&(code==='KeyW'||code==='ArrowUp')?128:paused&&(code==='KeyS'||code==='ArrowDown')?256:driving[code];
  }
  this.pressed.clear();return bits;
 }
}
