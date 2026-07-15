// Shared utility functions and UI helpers

// Concurrency-limited batch processor (semaphore/worker pool).
// Processes items with at most `concurrency` simultaneous async calls.
window.processBatch = async function(items, concurrency, fn) {
  let index = 0;
  async function worker() {
    while (index < items.length) {
      const i = index++;
      await fn(items[i], i);
    }
  }
  const workers = [];
  for (let i = 0; i < Math.min(concurrency, items.length); i++) {
    workers.push(worker());
  }
  await Promise.all(workers);
};

// Badge helpers
window.dnsStatusBadge = function(status) {
  if (!status) return '<span class="badge badge-info">...</span>';
  const m = {'complete':'badge-ok','partial':'badge-warn','failed':'badge-err'};
  return `<span class="badge ${m[status]||'badge-info'}">${esc(status)}</span>`;
};

window.runtimeStatusBadge = function(status) {
  if (!status) return '<span class="badge badge-info">...</span>';
  const m = {'Running':'badge-ok','Healthy':'badge-ok','Active':'badge-ok',
    'Stopped':'badge-err','Unhealthy':'badge-warn','Starting':'badge-warn',
    'Expiring':'badge-warn','Error':'badge-err','Expired':'badge-err',
    'Disabled':'badge-info','Issuing':'badge-warn','Unknown':'badge-info'};
  return `<span class="badge ${m[status]||'badge-info'}">${esc(status)}</span>`;
};

window.healthGradeBadge = function(score, grade) {
  if (score == null) return '<span class="badge badge-info">...</span>';
  const colors = {'Excellent':'badge-ok','Good':'badge-info',
    'Fair':'badge-warn','Poor':'badge-err','Critical':'badge-err'};
  return `<span class="badge ${colors[grade]||'badge-info'}">${score}%</span>`;
};

window.healthGradeLabel = function(score) {
  if (score >= 90) return 'Excellent';
  if (score >= 70) return 'Good';
  if (score >= 40) return 'Fair';
  if (score >= 1) return 'Poor';
  return 'Critical';
};

// Context-aware Health Score (frontend-calculated for v1).
// TODO: Move to backend as single source of truth.
//       Frontend should only display API-provided health_score field.
window.computeDomainHealthScore = function(r, dnsResult) {
  const checks = [];
  checks.push({id:'a', label:'A record', weight:25,
    ok: dnsResult && dnsResult.per_type &&
        dnsResult.per_type.some(pt => pt.type === 'A' && pt.status_code === 'NOERROR')});
  if (r.mail_domain_id && r.mail_domain_id > 0 && r.dkim_generated) {
    checks.push({id:'dkim', label:'DKIM', weight:10,
      ok: dnsResult && dnsResult.per_type &&
          dnsResult.per_type.some(pt => pt.type === 'TXT' && pt.status_code === 'NOERROR')});
  }
  if (r.site_id !== undefined) {
    checks.push({id:'ssl', label:'SSL', weight:20,
      ok: r.ssl_status === 'Active' || r.ssl_status === 'Expiring'});
  }
  const applicable = checks.filter(c => c.weight > 0);
  if (applicable.length === 0) return {score: null, grade: 'N/A'};
  const totalWeight = applicable.reduce((s, c) => s + c.weight, 0);
  const earned = applicable.reduce((s, c) => s + (c.ok ? c.weight : 0), 0);
  const score = Math.round((earned / totalWeight) * 100);
  const grade = window.healthGradeLabel(score);
  return {score, grade};
};
