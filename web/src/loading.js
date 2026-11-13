export async function fetchBytes(url,total,onProgress=()=>{}){
 const response=await fetch(url);if(!response.ok)throw Error(`Download failed (${response.status}): ${url}`);
 const reader=response.body?.getReader();if(!reader){const bytes=new Uint8Array(await response.arrayBuffer());onProgress(bytes.length);return bytes;}
 const chunks=[];let received=0;onProgress(0);
 while(true){const {done,value}=await reader.read();if(done)break;chunks.push(value);received+=value.length;if(total&&received>total){await reader.cancel();throw Error('Download exceeds expected size');}onProgress(received);}
 const bytes=new Uint8Array(received);let offset=0;for(const chunk of chunks){bytes.set(chunk,offset);offset+=chunk.length;}return bytes;
}

export class LoadingScreen{
 constructor(){this.root=document.getElementById('loading');this.label=document.getElementById('loading-label');this.bar=document.getElementById('loading-bar');this.percent=document.getElementById('loading-percent');this.size=document.getElementById('loading-size');this.items=new Map();}
 begin(label,entries){this.root.hidden=false;this.root.setAttribute('aria-busy','true');this.label.textContent=label;this.items=new Map(entries.map(e=>[e.key,{bytes:e.bytes,loaded:0}]));this.bar.classList.remove('progress-bar-striped','progress-bar-animated');this.render();}
 progress({key,loaded}){const item=this.items.get(key);if(this.root.hidden||!item)return;item.loaded=Math.min(item.bytes,Math.max(item.loaded,loaded));this.render();}
 render(){let total=0,loaded=0;for(const item of this.items.values()){total+=item.bytes;loaded+=item.loaded;}const percent=total?Math.floor(loaded/total*100):100;this.percent.textContent=`${percent}%`;this.bar.style.width=`${percent}%`;this.bar.classList.toggle('progress-bar-striped',!total);this.bar.classList.toggle('progress-bar-animated',!total);this.bar.parentElement.setAttribute('aria-valuenow',String(percent));this.size.textContent=`${(loaded/1e6).toFixed(2)} / ${(total/1e6).toFixed(2)} MB${total?'':' · Cached'}`;this.root.dataset.loadedBytes=String(loaded);this.root.dataset.totalBytes=String(total);}
 preparing(label){this.label.textContent=label;this.bar.classList.add('progress-bar-striped','progress-bar-animated');}
 finish(){this.root.hidden=true;this.root.setAttribute('aria-busy','false');}
}
