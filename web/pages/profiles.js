import {
  api, buildTable, esc, pageHeader, summaryCards, tb
} from '../core/context.js';


async function loadProfiles(p) {
  try {
    const data = await api('/api/profiles');
    const rows = data.data || [];
    p.innerHTML = pageHeader('Configuration Profiles', 'Available hosting configuration profiles and defaults.', '', 'Configuration')
      + summaryCards([
        {label:'Profiles', value:rows.length, tone:'neutral', help:'Profile records'},
        {label:'Enabled', value:rows.filter(r => r.enabled).length, tone:'healthy', help:'Available for use'},
        {label:'Defaults', value:rows.filter(r => r.default).length, tone:'info', help:'Default selections'}
      ]);
    p.innerHTML += tb('All Profiles') + buildTable([
      {label:'Name',html:r=>esc(r.name)},{label:'Type',html:r=>esc(r.type)},{label:'Web Server',html:r=>esc(r.web_server)},{label:'Default',html:r=>r.default?'<span class="badge badge-ok">Yes</span>':''},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'}
    ], rows);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load profiles</div>'; }
}

const profilesPage = { mount: loadProfiles };
export { loadProfiles, profilesPage };
