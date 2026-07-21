import {
  api, esc, pageHeader, qsa, summaryCards
} from '../core/context.js';


/* ===== DASHBOARD ===== */
async function loadDashboard(p, params, lifecycle) {
  try {
    const [health, sites, jobs] = await Promise.all([
      api('/api/health'), api('/api/sites'), api('/api/jobs')
    ]);
    if (lifecycle && !lifecycle.isActive()) return;
    const ok = health.data?.status === 'ok';
    const recentJobs = (jobs.data||[]).slice(-5).reverse();
    const failedJobs = recentJobs.filter(j => j.status === 'failed').length;
    const runningJobs = recentJobs.filter(j => j.status !== 'completed' && j.status !== 'failed').length;
    p.innerHTML = `
      ${pageHeader('Dashboard', 'Operational overview for sites, services, recent jobs, and quick administration paths.', '<button class="btn btn-sm" onclick="navigate(\'sites\')">Sites</button><button class="btn btn-sm" onclick="navigate(\'databases\')">Databases</button><button class="btn btn-sm" onclick="navigate(\'proxy\')">Proxy</button>', 'System overview')}
      ${summaryCards([
        {label:'Total Sites', value:(sites.data||[]).length, tone:'neutral', help:'Managed site records'},
        {label:'Domains', value:'loading...', tone:'neutral', help:'Domain records'},
        {label:'Backups', value:'loading...', tone:'neutral', help:'Backup records'},
        {label:'SSL', value:'loading...', tone:'neutral', help:'Certificate records'},
        {label:'Active Jobs', value:runningJobs, tone:runningJobs ? 'warning' : 'healthy', help:'Recent non-terminal jobs'},
        {label:'Critical Jobs', value:failedJobs, tone:failedJobs ? 'critical' : 'healthy', help:'Recent failed jobs'}
      ])}
      <div class="health-grid">
        <div class="health-item"><div class="health-dot ${ok?'ok':'error'}"></div><div><div class="health-name">Daemon</div><div class="health-label">${ok?'Running':'Unavailable'}</div></div></div>
        <div class="health-item"><div class="health-dot ok"></div><div><div class="health-name">REST API</div><div class="health-label">Online</div></div></div>
        <div class="health-item"><div class="health-dot ok"></div><div><div class="health-name">Storage</div><div class="health-label">Loaded</div></div></div>
        <div class="health-item" id="mail-health-dot"><div class="health-dot"></div><div><div class="health-name">Mail</div><div class="health-label">loading...</div></div></div>
      </div>
      <div style="font-size:13px;color:var(--text2);margin-bottom:12px;font-weight:600;">Recent Jobs</div>
      <div class="activity-list">
        ${recentJobs.length ? recentJobs.map(j => `<div class="activity-item"><div class="activity-icon" style="background:${j.status==='completed'?'var(--green)':j.status==='failed'?'var(--red)':'var(--yellow)'}"></div><div class="activity-text">${esc(j.type)}: ${esc(j.message||j.status)}</div><div class="activity-time">${j.progress}%</div></div>`).join('') : '<div class="activity-item"><div class="activity-text" style="color:var(--text3)">No recent jobs</div></div>'}
      </div>`;
    Promise.all([api('/api/domains'), api('/api/backups'), api('/api/ssl')]).then(([d,b,s])=>{
      if (lifecycle && !lifecycle.isActive()) return;
      const cards = qsa('.ui-summary-value');
      if (cards.length >= 4) cards[1].textContent = (d.data||[]).length;
      if (cards.length >= 4) cards[2].textContent = (b.data||[]).length;
      if (cards.length >= 4) cards[3].textContent = (s.data||[]).length;
    }).catch(()=>{});
    // Mail health dot
    api('/api/mail/health').then(h => {
      if (lifecycle && !lifecycle.isActive()) return;
      const dot = $('mail-health-dot');
      if (!dot) return;
      const status = h.data?.status || 'error';
      const d = dot.querySelector('.health-dot');
      if (d) d.className = 'health-dot ' + (status === 'ok' ? 'ok' : status === 'degraded' ? 'warn' : 'error');
      const l = dot.querySelector('.health-label');
      if (l) l.textContent = status === 'ok' ? 'Healthy' : status === 'degraded' ? 'Warning' : 'Error';
    }).catch(() => {
      if (lifecycle && !lifecycle.isActive()) return;
      const dot = $('mail-health-dot');
      if (dot) { const d = dot.querySelector('.health-dot'); if(d) d.className = 'health-dot'; const l = dot.querySelector('.health-label'); if(l) l.textContent = 'Inactive'; }
    });
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load dashboard</div>'; }
}

const dashboardPage = { mount: loadDashboard };
export { loadDashboard, dashboardPage };
