import {
  api, buildTable, esc, tb
} from '../core/context.js';


async function loadNodes(p) {
  try {
    const data = await api('/api/nodes');
    p.innerHTML = `<div class="page-header"><h1>Nodes</h1></div>`;
    p.innerHTML += tb('Node Details') + buildTable([
      {label:'ID',html:r=>esc(r.id)},{label:'Name',html:r=>esc(r.name)},{label:'Type',html:r=>esc(r.type)}
    ], data.data||[], 'No nodes');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load nodes</div>'; }
}

export { loadNodes };
Object.assign(window, { loadNodes });
