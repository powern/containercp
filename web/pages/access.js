import {
  api, apiPost, buildTable, esc, pageHeader, summaryCards, tb, toast
} from '../core/context.js';


/* ===== ACCESS ===== */
let activeAccessLifecycle = null;

async function loadAccess(p, params, lifecycle) {
  activeAccessLifecycle = lifecycle || activeAccessLifecycle;
  try {
    const data = await api('/api/access-users');
    if (lifecycle && !lifecycle.isActive()) return;
    const rows = data.data || [];
    p.innerHTML = pageHeader('Access Users', 'Developer access inventory and removal actions.', '', 'Access')
      + summaryCards([
        {label:'Users', value:rows.length, tone:'neutral', help:'Access user records'},
        {label:'Enabled', value:rows.filter(r => r.enabled).length, tone:'healthy', help:'Can authenticate'},
        {label:'Disabled', value:rows.filter(r => !r.enabled).length, tone:'warning', help:'Present but disabled'}
      ]);
    p.innerHTML += tb('All Access Users');
    const render = () => {
      const tbl = $('access-table');
      if (!tbl) return;
      tbl.innerHTML = buildTable([
        {label:'Username',html:r=>esc(r.username)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'},
        {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeAccessUser('${esc(r.username)}')">&#10005;</button>`}
      ], rows);
    };
    if (lifecycle && lifecycle.setRenderTable) lifecycle.setRenderTable(render);
    else window.renderTable = render;
    p.innerHTML += `<div id="access-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load access users</div>'; }
}

async function removeAccessUser(username) {
  if (!confirm('Remove access user?')) return;
  try { const res = await apiPost('/api/access-users/remove',{username}); if(res.success){toast('User removed','success');loadAccess($('page'), null, activeAccessLifecycle);}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

const accessPage = { mount: loadAccess, unmount() { activeAccessLifecycle = null; } };
export { loadAccess, accessPage };
Object.assign(window, { removeAccessUser });
