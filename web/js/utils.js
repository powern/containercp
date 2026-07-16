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

// Format MX records for display: returns array of {target, priority, ttl}
// Used by Overview, DNS Records, and Mail tabs for consistent display.
window.formatMxRecords = function(mxRecords) {
  if (!Array.isArray(mxRecords) || mxRecords.length === 0) return [];
  return mxRecords.map(r => ({
    target: r.value || '',
    priority: r.priority || 0,
    ttl: r.ttl || 0
  }));
};

// Format MX records for Published column: target with priority below
window.formatMxPublished = function(records) {
  const fmtd = window.formatMxRecords(records);
  return fmtd.map(r => (r.priority ? r.target + ' (priority ' + r.priority + ')' : r.target)).join(', ');
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

// SPF: read from spf_analysis (backend), fallback to normalized comparison (legacy)
window.compareSpfRecords = function(recommended, published, spfAnalysis) {
  if (spfAnalysis && spfAnalysis.match && spfAnalysis.match !== 'not_published') {
    return {status: spfAnalysis.match === 'match' ? 'Match' : 'Mismatch',
            cls: spfAnalysis.match === 'match' ? 'badge-ok' : 'badge-warn'};
  }
  if (spfAnalysis && spfAnalysis.match === 'not_published') {
    return {status:'Not Published', cls:'badge-err'};
  }
  // Legacy fallback: normalized string comparison
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
// TODO: store result in HealthCache for cross-view consistency.
// Returns: {score, grade, breakdown[], earnedWeight, applicableWeight}
window.computeDomainHealthScore = function(ctx) {
  // ctx = { domainRow, mailDomain, rootDns, dkimDns, dmarcDns, mtaStsDns, autoDns,
  //          sslStatus, runtimeStatus, serverHostname }
  // If mailDomain is not passed but domainRow.mail_domain_id exists, builds partial mail.
  var row = ctx.domainRow || {};
  var mail = ctx.mailDomain;
  var rdns = ctx.rootDns;
  var ssl = ctx.sslStatus || row.ssl_status;
  var rt = ctx.runtimeStatus;
  var expectedIpv4 = rdns && rdns.expected_ipv4 || '';
  var expectedIpv6 = rdns && rdns.expected_ipv6 || '';
  var spf = rdns && rdns.spf_analysis;
  var serverHost = ctx.serverHostname || '';

  // Detect mail from domainRow if mailDomain not passed
  if (!mail && row.mail_domain_id && row.mail_domain_id > 0) {
    mail = { domain: row.domain, mode: 'local-primary', dkim_public_key_dns: row.dkim_generated ? row.dkim_public_key_dns : '' };
  }
  var hasMail = !!(mail && mail.mode && mail.mode !== 'disabled');

  function getRecs(typeName) {
    if (!rdns || !Array.isArray(rdns.per_type)) return [];
    var pt = rdns.per_type.find(function(x) { return x && x.type === typeName; });
    if (!pt || !Array.isArray(pt.records)) return [];
    return pt.records;
  }

  // Partial credit mapping
  function computeEarned(weight, status, cls) {
    if (status === 'Match' || status === 'Active' || status === 'Running' || status === 'Healthy') return weight;
    if (status === 'Expiring') return Math.round(weight * 0.5);
    if (status === 'Starting') return Math.round(weight * 0.5);
    if (status === 'N/A') return null; // excluded from denominator
    if (status === 'Unexpected') return 0;
    return 0; // Mismatch, Not Published, Expired, Stopped, Error, Unknown
  }

  function checkConfig(id, label, cls, weight, status, configured, published) {
    var earned = computeEarned(weight, status, cls);
    return { id:id, label:label, cls:cls, weight:weight, status:status,
             configured:configured || '', published:published || '',
             earned: earned, applicable: earned !== null };
  }

  var checks = [];
  var now = new Date().toISOString();

  // 1. A record (required, 25)
  var aRecs = getRecs('A');
  var aPub = aRecs.map(function(r) { return r.value; }).join(', ');
  var aHasExpected = !!expectedIpv4;
  var aMatch = aHasExpected && aRecs.some(function(r) { return r.value === expectedIpv4; });
  var aStatus = aHasExpected ? (aMatch ? 'Match' : (aRecs.length ? 'Mismatch' : 'Not Published')) : (aRecs.length ? 'Unexpected' : 'N/A');
  checks.push(checkConfig('a', 'A record', 'req', 25, aStatus, expectedIpv4, aPub));

  // 2. AAAA (informational, 0)
  var aaaaRecs = getRecs('AAAA');
  var aaaaPub = aaaaRecs.map(function(r) { return r.value; }).join(', ');
  var aaaaStatus = expectedIpv6 ? (aaaaRecs.length ? 'Match' : 'Not Published') : 'N/A';
  checks.push(checkConfig('aaaa', 'AAAA (IPv6)', 'info', 0, aaaaStatus, expectedIpv6, aaaaPub));

  // 3–6. Mail checks
  if (hasMail) {
    // 3. MX (required, 12)
    var mxRecs = getRecs('MX');
    var expectedMx = window.getExpectedMxTarget(mail, serverHost);
    var mxPub = mxRecs.map(function(r) { return (r.priority ? r.priority + ' ' : '') + r.value; }).join(', ');
    var mxMatch = expectedMx && mxRecs.some(function(r) { return window.normalizeHostname(r.value) === window.normalizeHostname(expectedMx); });
    var mxStatus = expectedMx ? (mxMatch ? 'Match' : (mxRecs.length ? 'Mismatch' : 'Not Published')) : 'N/A';
    checks.push(checkConfig('mx', 'MX', 'req', 12, mxStatus, expectedMx, mxPub));

    // 4. SPF (required, 10) — from spf_analysis
    if (spf && spf.match) {
      var spfStatus = spf.match === 'match' ? 'Match' : spf.match === 'error' ? 'Error' : spf.match === 'not_published' ? 'Not Published' : 'Mismatch';
      checks.push(checkConfig('spf', 'SPF', 'req', 10, spfStatus, 'v=spf1 mx ~all', spf.record || ''));
    } else {
      var spfRecs = getRecs('TXT').filter(function(r) { return typeof r.value === 'string' && r.value.indexOf('v=spf1') === 0; });
      var spfPub = spfRecs.length > 0 ? spfRecs[0].value : '';
      checks.push(checkConfig('spf', 'SPF', 'req', 10, spfPub ? 'Unexpected' : 'N/A', 'v=spf1 mx ~all', spfPub));
    }

    // 5. DKIM (required, 10 — only if generated)
    var dkimKey = mail.dkim_public_key_dns || '';
    if (dkimKey) {
      var dkimRecs = [];
      if (ctx.dkimDns) {
        var dpt = ctx.dkimDns.per_type && ctx.dkimDns.per_type.find(function(x) { return x && x.type === 'TXT'; });
        if (dpt && Array.isArray(dpt.records)) dkimRecs = dpt.records;
      }
      var dkimPub = dkimRecs.length > 0 ? dkimRecs[0].value : '';
      var dkimMatch = dkimPub && window.normalizeDnsValue(dkimPub) === window.normalizeDnsValue(dkimKey);
      var dkimStatus = dkimMatch ? 'Match' : (dkimPub ? 'Mismatch' : 'Not Published');
      checks.push(checkConfig('dkim', 'DKIM', 'req', 10, dkimStatus, dkimKey, dkimPub));
    }

    // 6. DMARC (required, 8) — any valid DMARC policy is acceptable
    var dmarcPub = '';
    if (ctx.dmarcDns) {
      var dpt = ctx.dmarcDns.per_type && ctx.dmarcDns.per_type.find(function(x) { return x && x.type === 'TXT'; });
      if (dpt && Array.isArray(dpt.records) && dpt.records.length > 0) dmarcPub = dpt.records[0].value;
    }
    var dmarcValid = dmarcPub && dmarcPub.indexOf('v=DMARC1') === 0;
    var dmarcStatus = dmarcValid ? 'Match' : (dmarcPub ? 'Error' : 'Not Published');
    checks.push(checkConfig('dmarc', 'DMARC', 'req', 8, dmarcStatus, 'v=DMARC1; p=none;', dmarcPub));

    // Recommended mail checks (only for local-primary)
    if (mail.mode === 'local-primary') {
      // MTA-STS (recommended, 3)
      var stsPub = '';
      if (ctx.mtaStsDns) {
        var spt = ctx.mtaStsDns.per_type && ctx.mtaStsDns.per_type.find(function(x) { return x && x.type === 'TXT'; });
        if (spt && Array.isArray(spt.records) && spt.records.length > 0) stsPub = spt.records[0].value;
      }
      checks.push(checkConfig('mta-sts', 'MTA-STS', 'rec', 3, stsPub ? 'Match' : 'Not Published', 'v=STSv1; id=1', stsPub));

      // Autodiscover (recommended, 3) — real check
      var autoRecs = [];
      if (ctx.autoDns) {
        var cpt = ctx.autoDns.per_type && ctx.autoDns.per_type.find(function(x) { return x && (x.type === 'CNAME' || x.type === 'A'); });
        if (cpt && Array.isArray(cpt.records)) autoRecs = cpt.records;
      }
      var autoPub = autoRecs.length > 0 ? autoRecs[0].value : '';
      var autoStatus = serverHost ? (autoPub ? 'Match' : 'Not Published') : 'N/A';
      checks.push(checkConfig('autodiscover', 'Autodiscover', 'rec', 3, autoStatus, 'CNAME → ' + serverHost, autoPub));
    }
  }

  // 7. SSL Certificate (required, 20) — ALL domains
  checks.push(checkConfig('ssl', 'SSL Certificate', 'req', 20, ssl || 'Unknown', '—', ssl || 'Unknown'));

  // 8. Runtime Status (required, 15) — site_id > 0 only
  if (row.site_id > 0) {
    var rtStatus = rt || 'Unknown';
    checks.push(checkConfig('runtime', 'Runtime Status', 'req', 15, rtStatus, '—', rtStatus));
  }

  // 9. CAA (recommended, 2)
  var caaRecs = getRecs('CAA');
  var caaPub = caaRecs.map(function(r) { return r.value; }).join(', ');
  var caaStatus = caaRecs.length > 0 ? 'Match' : 'Not Published';
  checks.push(checkConfig('caa', 'CAA', 'rec', 2, caaStatus, '0 issue "letsencrypt.org"', caaPub));

  // Compute score
  var applicable = checks.filter(function(c) { return c.applicable && c.weight > 0; });
  if (applicable.length === 0) return {score: null, grade: 'N/A', breakdown: checks, earnedWeight: 0, applicableWeight: 0, computed_at: now};

  var totalWeight = applicable.reduce(function(s, c) { return s + c.weight; }, 0);
  var earnedWeight = applicable.reduce(function(s, c) { return s + c.earned; }, 0);
  var score = Math.round((earnedWeight / totalWeight) * 100);
  var grade = window.healthGradeLabel(score);

  return {score: score, grade: grade, breakdown: checks, earnedWeight: earnedWeight, applicableWeight: totalWeight, computed_at: now};
};
