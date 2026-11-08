(async()=>{
const f=window.fixture,wait=()=>new Promise(r=>setTimeout(r,60));const check=(v,m)=>{if(!v)throw Error(m);};
check(!document.querySelector('input'),'initial hidden');
f.values['bluetooth/config']={enable:true};f.event('bluetooth/config');await wait();check(!document.querySelector('input'),'config gate');
f.values['bluetooth/remote_config']={enable:false};f.values['bluetooth/state']={phase:'active',supported:true};f.values['bluetooth/remote_state']={busy:false,phase:'idle',generation:1,remaining_ms:0,enrollment_ready:false};f.event('bluetooth/remote_config');await wait();
check(!!document.querySelector('input.form-check-input'),'native switch');check(f.calls.length===0,'mount passive');
document.querySelector('input').click();await wait();check(f.calls.length===1&&f.calls[0].t==='bluetooth/remote_config'&&f.calls[0].p.enable,'enable save only');
document.querySelector('[data-action=remote-start]').click();await wait();check(f.calls.at(-1).t==='bluetooth/remote_start','explicit start');
f.values['bluetooth/remote_state']={busy:true,phase:'scanning',generation:9,remaining_ms:28000,enrollment_ready:false};f.values['bluetooth/remote_candidates']={entries:[{index:0,address:'01:02:03:04:05:06',address_type:1}]};f.event('bluetooth/remote_candidates');await wait();
document.querySelector('[data-action=remote-select]').click();await wait();check(f.calls.at(-1).t==='bluetooth/remote_select'&&f.calls.at(-1).p.generation===9,'select generation');
f.values['bluetooth/remote_state']={busy:false,phase:'consent_untrusted',generation:9,remaining_ms:0,enrollment_ready:false};f.event('bluetooth/remote_state');await wait();check(!!document.querySelector('.alert-warning')&&!document.querySelector('.alert-success'),'no trusted success');
f.values['bluetooth/remote_state']={busy:false,phase:'paired_tofu',generation:9,remaining_ms:0,enrollment_ready:true,trusted:false};f.event('bluetooth/remote_state');await wait();check(!document.querySelector('.alert-success'),'phase alone not success');
f.values['bluetooth/remote_state'].trusted=true;f.event('bluetooth/remote_state');await wait();check(!!document.querySelector('.alert-success'),'mutual committed success');check(document.body.innerText.includes('tesla_vehicle.remote.tofu'),'TOFU boundary visible');
f.values['bluetooth/remote_bindings']={entries:[{id:'a'.repeat(64),name:'fixture Tag'}]};f.event('bluetooth/remote_bindings');await wait();
document.querySelector('[data-action=remote-forget]').click();await wait();check(f.calls.at(-1).t==='bluetooth/remote_forget'&&f.calls.at(-1).p.confirmed===true,'individually confirmed forget');
f.values['bluetooth/remote_bindings']={entries:[{id:'b'.repeat(64),name:'Pending Tag',phase:'prepared'}]};f.event('bluetooth/remote_bindings');await wait();check(document.body.innerText.includes('tesla_vehicle.remote.pending'),'pending not trusted');
document.querySelector('[data-action=remote-recover]').click();await wait();check(f.calls.at(-1).t==='bluetooth/remote_recover'&&f.calls.at(-1).p.confirmed===true,'confirmed recovery');
f.values['bluetooth/config']={enable:false};f.event('bluetooth/config');await wait();check(!document.querySelector('input'),'global off hides');
return {pass:true,calls:f.calls,leaves:'actual Preact/react-bootstrap/FormRow/Switch DOM'};
})()
