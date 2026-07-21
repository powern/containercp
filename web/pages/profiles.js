import {
  api, buildTable, esc, tb
} from '../core/context.js';


async function loadProfiles(p) {
  try {
    const data = await api('/api/profiles');
    p.innerHTML = `<div class="page-header"><h1>Configuration Profiles</h1></div>`;
    p.innerHTML += tb('All Profiles') + buildTable([
      {label:'Name',html:r=>esc(r.name)},{label:'Type',html:r=>esc(r.type)},{label:'Web Server',html:r=>esc(r.web_server)},{label:'Default',html:r=>r.default?'<span class="badge badge-ok">Yes</span>':''},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'}
    ], data.data||[]);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load profiles</div>'; }
}

const profilesPage = { mount: loadProfiles };
export { loadProfiles, profilesPage };
