import {
  api, esc, qsa
} from '../core/context.js';


/* ===== DASHBOARD ===== */
async function loadDashboard(p) {
  try {
    const [health, sites, jobs] = await Promise.all([
      api('/api/health'), api('/api/sites'), api('/api/jobs')
    ]);
    const ok = health.data?.status === 'ok';
    const recentJobs = (jobs.data||[]).slice(-5).reverse();
    p.innerHTML = `
      <div class="page-header"><h1>Dashboard</h1></div>
      <div class="cards">
        ${card('Sites', (sites.data||[]).length, '#6366f1')}
        ${card('Domains', 0, '#3b82f6', 'loading...')}
        ${card('Backups', 0, '#8b5cf6', 'loading...')}
        ${card('SSL', 0, '#ec4899', 'loading...')}
      </div>
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
      const cards = qsa('.card .count');
      if (cards.length >= 4) { cards[1].textContent = (d.data||[]).length; cards[1].className='count'+(cards[1].textContent==='0'?' zero':''); }
      if (cards.length >= 4) { cards[2].textContent = (b.data||[]).length; cards[2].className='count'+(cards[2].textContent==='0'?' zero':''); }
      if (cards.length >= 4) { cards[3].textContent = (s.data||[]).length; cards[3].className='count'+(cards[3].textContent==='0'?' zero':''); }
    }).catch(()=>{});
    // Mail health dot
    api('/api/mail/health').then(h => {
      const dot = $('mail-health-dot');
      if (!dot) return;
      const status = h.data?.status || 'error';
      const d = dot.querySelector('.health-dot');
      if (d) d.className = 'health-dot ' + (status === 'ok' ? 'ok' : status === 'degraded' ? 'warn' : 'error');
      const l = dot.querySelector('.health-label');
      if (l) l.textContent = status === 'ok' ? 'Healthy' : status === 'degraded' ? 'Warning' : 'Error';
    }).catch(() => {
      const dot = $('mail-health-dot');
      if (dot) { const d = dot.querySelector('.health-dot'); if(d) d.className = 'health-dot'; const l = dot.querySelector('.health-label'); if(l) l.textContent = 'Inactive'; }
    });
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load dashboard</div>'; }
}

function card(label, count, color, sub) {
  return `<div class="card"><div class="card-header"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="2"><circle cx="12" cy="12" r="10"/></svg><h3>${label}</h3></div><div class="count${count===0?' zero':''}">${sub||count}</div></div>`;
}

export { loadDashboard };
Object.assign(window, { loadDashboard, card });
