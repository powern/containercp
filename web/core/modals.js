import { esc } from './utils.js';

export function showModal(title, bodyHtml, width) {
  let overlay = document.getElementById('modal-overlay');
  if (!overlay) {
    overlay = document.createElement('div'); overlay.id = 'modal-overlay';
    overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,.5);display:flex;align-items:center;justify-content:center;z-index:1000;';
    overlay.addEventListener('click', e => { if (e.target === overlay) hideModal(); });
    document.body.appendChild(overlay);
  }
  overlay.innerHTML = '<div style="background:var(--surface);border:1px solid var(--border);border-radius:12px;width:' + (width||480) + 'px;max-width:90vw;max-height:80vh;overflow-y:auto;">'
    + '<div style="padding:18px 20px;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center;"><h3 style="margin:0;font-size:16px;">' + esc(title) + '</h3><button class="btn-icon" onclick="hideModal()">&times;</button></div>'
    + '<div style="padding:20px;">' + bodyHtml + '</div></div>';
  overlay.style.display = 'flex';
}

export function hideModal() { const o=document.getElementById('modal-overlay'); if(o)o.style.display='none'; }
export function destroyModal() { const o=document.getElementById('modal-overlay'); if(o)o.remove(); }
