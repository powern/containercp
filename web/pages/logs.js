import {
  api, buildTable, esc, pageHeader, summaryCards, tb
} from '../core/context.js';


async function loadLogs(p) {
  try {
    const data = await api('/api/logs');
    const rows = data.data || [];
    p.innerHTML = pageHeader('Logs', 'Recent daemon and platform log messages.', '<button class="btn btn-sm" onclick="loadLogs($(\'page\'))">Refresh</button>', 'Observability')
      + summaryCards([
        {label:'Log Lines', value:rows.length, tone:'neutral', help:'Current result set'},
        {label:'Warnings', value:rows.filter(r => r.level === 'warn').length, tone:'warning', help:'Warning entries'},
        {label:'Errors', value:rows.filter(r => r.level === 'error').length, tone:'critical', help:'Error entries'}
      ]);
    p.innerHTML += tb('System Logs') + buildTable([
      {label:'Time',html:r=>esc(r.time)},{label:'Level',html:r=>{let m={info:'badge-info',warn:'badge-warn',error:'badge-err'};return `<span class="badge ${m[r.level]||'badge-info'}">${esc(r.level)}</span>`;}},{label:'Message',html:r=>esc(r.message)}
    ], rows, 'No logs');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load logs</div>'; }
}

const logsPage = { mount: loadLogs };
export { loadLogs, logsPage };
Object.assign(window, { loadLogs });
