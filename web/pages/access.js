import {
  api, apiPost, buildTable, esc, tb, toast
} from '../core/context.js';


/* ===== ACCESS ===== */
async function loadAccess(p) {
  try {
    const data = await api('/api/access-users');
    p.innerHTML = `<div class="page-header"><h1>Access Users</h1></div>`;
    p.innerHTML += tb('All Access Users');
    window.renderTable = () => {
      const tbl = $('access-table');
      if (!tbl) return;
      tbl.innerHTML = buildTable([
        {label:'Username',html:r=>esc(r.username)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'},
        {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeAccessUser('${esc(r.username)}')">&#10005;</button>`}
      ], data.data||[]);
    };
    p.innerHTML += `<div id="access-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load access users</div>'; }
}

async function removeAccessUser(username) {
  if (!confirm('Remove access user?')) return;
  try { const res = await apiPost('/api/access-users/remove',{username}); if(res.success){toast('User removed','success');loadAccess($('page'));}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

export { loadAccess };
Object.assign(window, { loadAccess, removeAccessUser });
