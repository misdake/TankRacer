// Browser help/settings only. Original CUP selection remains in C.
export class GameTools{
 constructor({onOpen,onUnlock,onOpponents}){
  this.root=document.getElementById('game-tools');this.unlock=document.getElementById('unlock-all');
  this.buttons=[...this.root.querySelectorAll('[data-bs-toggle="dropdown"]')];
  this.menus=this.buttons.map(button=>globalThis.bootstrap.Dropdown.getOrCreateInstance(button));
  this.root.addEventListener('show.bs.dropdown',onOpen);
  this.root.addEventListener('hidden.bs.dropdown',()=>queueMicrotask(()=>{
   if(!this.root.querySelector('.dropdown-menu.show')&&this.root.contains(document.activeElement))document.getElementById('game').focus({preventScroll:true});
  }));
  this.unlock.addEventListener('click',async()=>{
   this.unlock.disabled=true;this.unlock.textContent='Unlocking…';
   try{await onUnlock();}catch{this.unlock.disabled=false;this.unlock.textContent='Unlock all';}
  });
  const opponents=document.getElementById('no-opponents');
  opponents.addEventListener('click',()=>{const enabled=opponents.getAttribute('aria-pressed')!=='true';opponents.setAttribute('aria-pressed',String(enabled));opponents.classList.toggle('active',enabled);onOpponents(enabled);});
 }
 update(menu,unlocked=false){
  if(!menu)for(const dropdown of this.menus)dropdown.hide();
  this.root.hidden=!menu;
  if(unlocked){this.unlock.disabled=true;this.unlock.textContent='All unlocked';}
 }
 ownsKey(event){
  if(this.root.contains(event.target))return true;
  if(event.code==='Escape'&&this.root.querySelector('.dropdown-menu.show')){
   for(const dropdown of this.menus)dropdown.hide();event.preventDefault();return true;
  }
  return false;
 }
}
