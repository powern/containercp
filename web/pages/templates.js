import {
  api, buildTable, esc, tb
} from '../core/context.js';


async function loadTemplates(p) {
  try {
    const data = await api('/api/profiles');
    const web = (data.data||[]).filter(r => r.type === 'web_server');
    p.innerHTML = `<div class="page-header"><h1>Web Server Templates</h1></div>`;
    p.innerHTML += tb('Templates') + buildTable([
      {label:'Name',html:r=>esc(r.name)},{label:'Web Server',html:r=>esc(r.web_server)},{label:'Valid',html:r=>'<span class="badge badge-ok">Valid</span>'}
    ], web, 'No templates');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load templates</div>'; }
}

export { loadTemplates };
Object.assign(window, { loadTemplates });
