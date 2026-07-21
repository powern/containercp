import {
  api, buildTable, esc, pageHeader, summaryCards, tb
} from '../core/context.js';


async function loadTemplates(p) {
  try {
    const data = await api('/api/profiles');
    const web = (data.data||[]).filter(r => r.type === 'web_server');
    p.innerHTML = pageHeader('Web Server Templates', 'Web server profile templates available for site provisioning.', '', 'Templates')
      + summaryCards([
        {label:'Templates', value:web.length, tone:'neutral', help:'Web server templates'},
        {label:'Nginx', value:web.filter(r => r.web_server === 'nginx').length, tone:'info', help:'Nginx-backed templates'},
        {label:'Apache', value:web.filter(r => r.web_server !== 'nginx').length, tone:'healthy', help:'Apache-backed templates'}
      ]);
    p.innerHTML += tb('Templates') + buildTable([
      {label:'Name',html:r=>esc(r.name)},{label:'Web Server',html:r=>esc(r.web_server)},{label:'Valid',html:r=>'<span class="badge badge-ok">Valid</span>'}
    ], web, 'No templates');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load templates</div>'; }
}

const templatesPage = { mount: loadTemplates };
export { loadTemplates, templatesPage };
