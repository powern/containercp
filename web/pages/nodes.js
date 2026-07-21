import {
  api, buildTable, esc, pageHeader, summaryCards, tb
} from '../core/context.js';


async function loadNodes(p) {
  try {
    const data = await api('/api/nodes');
    const rows = data.data || [];
    p.innerHTML = pageHeader('Nodes', 'Registered node inventory for the control panel.', '', 'Infrastructure')
      + summaryCards([{label:'Nodes', value:rows.length, tone:'neutral', help:'Registered infrastructure nodes'}]);
    p.innerHTML += tb('Node Details') + buildTable([
      {label:'ID',html:r=>esc(r.id)},{label:'Name',html:r=>esc(r.name)},{label:'Type',html:r=>esc(r.type)}
    ], rows, 'No nodes');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load nodes</div>'; }
}

const nodesPage = { mount: loadNodes };
export { loadNodes, nodesPage };
