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

// ===== Shared DNS Record Helpers =====

// Get records for a specific DNS type from a dnsResult object
window.getDnsRecs = function(dnsResult, typeName) {
  if (!dnsResult || !Array.isArray(dnsResult.per_type)) return [];
  const pt = dnsResult.per_type.find(x => x && x.type === typeName);
  if (!pt || !Array.isArray(pt.records)) return [];
  return pt.records;
};

// Format a value for display (truncate if too long)
window.fmtVal = function(v, max) {
  if (!v || typeof v !== 'string') return '—';
  if (v.length > (max || 40)) return esc(v.substr(0, max || 40)) + '...';
  return esc(v);
};

// Render a status badge
window.statusBadge = function(label, cls) {
  if (!label) return '<span class="badge badge-info">—</span>';
  return `<span class="badge ${cls || 'badge-info'}">${esc(label)}</span>`;
};

// Normalize hostname for comparison: lowercase, strip trailing dot
window.normalizeHostname = function(h) {
  if (!h || typeof h !== 'string') return '';
  h = h.toLowerCase();
  if (h.endsWith('.')) h = h.slice(0, -1);
  return h;
};

// Normalize a DMARC/TXT value for semantic comparison
// Splits by ';', trims each token, ignores empty trailing tokens,
// lowercase tag names, compares normalized set.
window.normalizeDmarcValue = function(v) {
  if (!v || typeof v !== 'string') return '';
  const tokens = v.split(';').map(t => t.trim().toLowerCase()).filter(t => t.length > 0);
  tokens.sort();  // normalize order
  return tokens.join(';');
};

// Normalize a DNS value for comparison: lowercase, strip whitespace
window.normalizeDnsValue = function(v) {
  if (!v || typeof v !== 'string') return '';
  return v.replace(/\s+/g, '');
};

// Build a full DNS record string (for Copy Full Record)
window.buildFullDnsRecord = function(fqdn, ttl, type, value, priority) {
  const name = fqdn.endsWith('.') ? fqdn : fqdn + '.';
  const ttlStr = (ttl && ttl > 0) ? ' ' + ttl : ' 3600';
  if (type === 'MX') {
    const prio = priority ? ' ' + priority : ' 10';
    return name + ttlStr + ' IN MX' + prio + ' ' + (value.endsWith('.') ? value : value + '.');
  }
  if (type === 'TXT' || type === 'SPF' || type === 'DKIM' || type === 'DMARC' || type === 'MTA-STS' || type === 'TLS-RPT') {
    return name + ttlStr + ' IN TXT "' + value + '"';
  }
  if (type === 'CAA') {
    return name + ttlStr + ' IN CAA ' + value;
  }
  if (type === 'CNAME') {
    return name + ttlStr + ' IN CNAME ' + (value.endsWith('.') ? value : value + '.');
  }
  return name + ttlStr + ' IN ' + type + ' ' + value;
};

// Generate data-copy buttons (Host, Value, FQDN, Full Record)
window.copyRowButtons = function(record) {
  const {host, type, value, ttl, priority, domainName} = record;
  const name = host === '@' ? domainName : host;
  const fqdn = name.endsWith('.') ? name : name + '.';
  const fullRecord = window.buildFullDnsRecord(name, ttl, type, value, priority);
  const escA = function(s) { return s.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/'/g, '&#39;'); };
  return `<span style="white-space:nowrap;">
    <button class="btn-icon" data-copy="${escA(name)}" title="Copy Host">H</button>
    <button class="btn-icon" data-copy="${escA(value)}" title="Copy Value">V</button>
    <button class="btn-icon" data-copy="${escA(fqdn)}" title="Copy FQDN">F</button>
    <button class="btn-icon" data-copy="${escA(fullRecord)}" title="Copy Full Record">R</button>
  </span>`;
};

// Attach data-copy event listener (single handler)
window.attachDataCopyListener = function(containerId) {
  const container = document.getElementById(containerId);
  if (!container) return;
  container.addEventListener('click', function(e) {
    const btn = e.target.closest('[data-copy]');
    if (!btn) return;
    const text = btn.getAttribute('data-copy');
    if (text) copyText(text, btn.getAttribute('title') || 'Copied');
  });
};

// Determine expected MX target based on MailDomain mode
// For local-primary: MX target = domain name itself (Postfix receives mail for the domain)
// For external-relay/split-m365: MX target = relay_host from MailDomain config
window.getExpectedMxTarget = function(mailDomain, serverHostname) {
  if (!mailDomain) return '';
  if (mailDomain.mode === 'local-primary') return mailDomain.domain || '';
  if (mailDomain.mode === 'external-relay' || mailDomain.mode === 'split-m365') return mailDomain.relay_host || '';
  return '';
};

// Compute DNS record status: Match/Mismatch/Not Published/Unexpected/N/A
window.computeRecordStatus = function(configuredVal, publishedVal, matchFn) {
  const hasCfg = !!configuredVal;
  const hasPub = !!publishedVal;
  if (hasCfg && hasPub) {
    const isMatch = matchFn ? matchFn(configuredVal, publishedVal) : (configuredVal === publishedVal);
    return {status: isMatch ? 'Match' : 'Mismatch', cls: isMatch ? 'badge-ok' : 'badge-warn'};
  }
  if (hasCfg && !hasPub) return {status: 'Not Published', cls: 'badge-err'};
  if (!hasCfg && hasPub) return {status: 'Unexpected', cls: 'badge-warn'};
  return {status: 'N/A', cls: 'badge-info'};
};

// ==== Shared DNS type comparison functions ====
// All return {status, cls} consistent across all tabs.

// A/AAAA: exact IP match across all records
window.compareIpRecords = function(records, expectedIp) {
  const publishedIps = records.map(r => r.value).filter(Boolean);
  const hasExpected = !!expectedIp;
  const hasPublished = publishedIps.length > 0;
  if (hasExpected && hasPublished) {
    const match = publishedIps.some(ip => ip === expectedIp);
    return match ? {status:'Match', cls:'badge-ok'} : {status:'Mismatch', cls:'badge-warn'};
  }
  if (hasExpected && !hasPublished) return {status:'Not Published', cls:'badge-err'};
  if (!hasExpected && hasPublished) return {status:'Unexpected', cls:'badge-warn'};
  return {status:'N/A', cls:'badge-info'};
};

// MX: exact hostname match among MX records (normalized, no substring)
window.compareMxRecords = function(mxRecords, expectedHostname) {
  const hasExpected = !!expectedHostname;
  const hasPublished = mxRecords.length > 0;
  if (hasExpected && hasPublished) {
    const normExp = window.normalizeHostname(expectedHostname);
    const match = mxRecords.some(r => window.normalizeHostname(r.value) === normExp);
    return match ? {status:'Match', cls:'badge-ok'} : {status:'Mismatch', cls:'badge-warn'};
  }
  if (hasExpected && !hasPublished) return {status:'Not Published', cls:'badge-err'};
  if (!hasExpected && hasPublished) return {status:'Unexpected', cls:'badge-warn'};
  return {status:'N/A', cls:'badge-info'};
};

// SPF: normalized DNS value comparison (placeholder until SpfAnalyzer)
window.compareSpfRecords = function(recommended, published) {
  return window.computeRecordStatus(recommended, published,
    (a,b) => window.normalizeDnsValue(a) === window.normalizeDnsValue(b));
};

// DKIM: normalized DNS value comparison
window.compareDkimRecords = function(configuredKey, publishedKey) {
  return window.computeRecordStatus(configuredKey, publishedKey,
    (a,b) => window.normalizeDnsValue(a) === window.normalizeDnsValue(b));
};

// DMARC: DMARC-semantic normalization
window.compareDmarcRecords = function(recommended, published) {
  return window.computeRecordStatus(recommended, published,
    (a,b) => window.normalizeDmarcValue(a) === window.normalizeDmarcValue(b));
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
