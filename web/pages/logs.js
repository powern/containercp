import {
  api, buildTable, esc, tb
} from '../core/context.js';


async function loadLogs(p) {
  try {
    const data = await api('/api/logs');
    p.innerHTML = `<div class="page-header"><h1>Logs</h1><div class="page-actions"><button class="btn btn-sm" onclick="loadLogs($('page'))">Refresh</button></div></div>`;
    p.innerHTML += tb('System Logs') + buildTable([
      {label:'Time',html:r=>esc(r.time)},{label:'Level',html:r=>{let m={info:'badge-info',warn:'badge-warn',error:'badge-err'};return `<span class="badge ${m[r.level]||'badge-info'}">${esc(r.level)}</span>`;}},{label:'Message',html:r=>esc(r.message)}
    ], data.data||[], 'No logs');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load logs</div>'; }
}

export { loadLogs };
Object.assign(window, { loadLogs });
