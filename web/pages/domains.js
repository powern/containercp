import {
  api, apiPost, buildTable, card, copyText, esc, escAttr, navigate, pageHeader, summaryCards, tb, toast
} from '../core/context.js';

const DnsCache = window.DnsCache;
const RuntimeCache = window.RuntimeCache;
const HealthCache = window.HealthCache;
const processBatch = window.processBatch;
const dnsStatusBadge = window.dnsStatusBadge;
const runtimeStatusBadge = window.runtimeStatusBadge;
const healthGradeBadge = window.healthGradeBadge;
const getDnsRecs = window.getDnsRecs;
const fmtVal = window.fmtVal;
const statusBadge = window.statusBadge;
const normalizeHostname = window.normalizeHostname;
const normalizeDmarcValue = window.normalizeDmarcValue;
const normalizeDnsValue = window.normalizeDnsValue;
const formatMxPublished = window.formatMxPublished;
const buildFullDnsRecord = window.buildFullDnsRecord;
const copyRowButtons = window.copyRowButtons;
const attachDataCopyListener = window.attachDataCopyListener;
const getExpectedMxTarget = window.getExpectedMxTarget;
const computeRecordStatus = window.computeRecordStatus;
const compareIpRecords = window.compareIpRecords;
const compareMxRecords = window.compareMxRecords;
const compareSpfRecords = window.compareSpfRecords;
const compareDkimRecords = window.compareDkimRecords;
const compareDmarcRecords = window.compareDmarcRecords;
const computeDomainHealthScore = window.computeDomainHealthScore;
let activeDomainsLifecycle = null;

/* ===== DOMAINS ===== */
function domainTypeBadge(type) {
  const m = {'primary':'badge-ok','alias':'badge-info','redirect':'badge-warn','wildcard':'badge-info','legacy':'badge-info','system':'badge-admin'};
  const label = type || 'legacy';
  return `<span class="badge ${m[label]||'badge-info'}">${esc(label)}</span>`;
}

function domainSslBadge(status) {
  const m={'Active':'badge-ok','Disabled':'badge-info','Expired':'badge-err','Expiring':'badge-warn','Error':'badge-err','Issuing':'badge-warn'};
  return `<span class="badge ${m[status]||'badge-info'}">${esc(status)}</span>`;
}

function domainUsableHttps(r) {
  // Only suggest HTTPS when the certificate is actually usable
  return r.ssl_status === 'Active' || r.ssl_status === 'Expiring';
}

async function loadDomains(p, params, lifecycle) {
  activeDomainsLifecycle = lifecycle || activeDomainsLifecycle;
  try {
    const data = await api('/api/domains');
    if (lifecycle && !lifecycle.isActive()) return;
    const domains = data.data || [];
    const linked = domains.filter(d => d.site_id && d.site_id > 0).length;
    const mailActive = domains.filter(d => d.mail_domain_id && d.mail_domain_id > 0).length;
    const sslUsable = domains.filter(domainUsableHttps).length;
    p.innerHTML = pageHeader('Domains', 'DNS, SSL, mail, runtime relationship, security, and health diagnostics.', `<span style="font-size:12px;color:var(--text3);font-weight:normal;">${domains.length} domain${domains.length===1?'':'s'}</span>`, 'DNS')
      + summaryCards([
        {label:'Domains', value:domains.length, tone:'neutral', help:'Known domain records'},
        {label:'Linked Sites', value:linked, tone:'healthy', help:'Attached to managed sites'},
        {label:'Usable HTTPS', value:sslUsable, tone:'healthy', help:'Active or expiring certificate'},
        {label:'Mail Active', value:mailActive, tone:'info', help:'Linked mail domains'}
      ]);
    p.innerHTML += tb('All Domains');

    const render = () => {
      const tbl = $('domains-table');
      if (!tbl) return;
      const lowerSearch = (window.searchTerm||'').toLowerCase();
      const rows = domains.filter(r => {
        if (!lowerSearch) return true;
        return (r.domain||'').toLowerCase().includes(lowerSearch)
            || (r.site_name||'').toLowerCase().includes(lowerSearch)
            || (r.site_domain||'').toLowerCase().includes(lowerSearch)
            || (r.target||'').toLowerCase().includes(lowerSearch)
            || (r.type||'').toLowerCase().includes(lowerSearch)
            || (r.ssl_status||'').toLowerCase().includes(lowerSearch);
      });
      tbl.innerHTML = buildTable([
        {label:'Domain', html: r => `<a href="#" onclick="navigate('domain-detail',${r.id});return false" style="color:var(--primary);text-decoration:none;font-weight:500;">${esc(r.domain)}</a> <span style="cursor:pointer;font-size:11px;color:var(--text3);" onclick="copyText('${esc(r.domain)}')" title="Copy domain">&#128203;</span>`},
        {label:'Type', html: r => domainTypeBadge(r.type)},
        {label:'Site', html: r => r.site_name ? `<div style="line-height:1.4;"><div>${esc(r.site_name)}</div><div style="font-size:11px;color:var(--text3);">${esc(r.site_domain||'')}</div></div>` : `<span class="badge badge-info">Unlinked</span>`},
        {label:'Target', html: r => {
          if (r.target) return `<span style="word-break:break-all;">${esc(r.target)}</span>`;
          if (r.site_domain) return `<span style="color:var(--text3);">${esc(r.site_domain)}</span>`;
          return '<span class="badge badge-info">—</span>';
        }},
        {label:'DNS', html: r => {
          const dnsData = DnsCache.get(r.domain, 'A,AAAA,MX');
          if (!dnsData) return '<span class="badge badge-info">...</span>';
          return window.dnsStatusBadge(dnsData.overall_status);
        }},
        {label:'Mail', html: r => r.mail_domain_id && r.mail_domain_id > 0 ? '<span class="badge badge-ok">Active</span>' : '<span class="badge badge-info">—</span>'},
        {label:'Runtime', html: r => {
          if (r.site_id === 0) return '<span class="badge badge-info">N/A</span>';
          if (!r.site_id && r.site_id !== 0) return '<span class="badge badge-info">N/A</span>';
          const rtData = RuntimeCache.get(r.site_id);
          if (!rtData) return '<span class="badge badge-info">...</span>';
          return window.runtimeStatusBadge(rtData.web);
        }},
        {label:'SSL', html: r => domainSslBadge(r.ssl_status)},
        {label:'Health', html: r => {
          var cachedHealth = window.HealthCache.get(r.domain);
          if (cachedHealth && cachedHealth !== 'loading') return window.healthGradeBadge(cachedHealth.score, cachedHealth.grade);
          const dnsData = DnsCache.get(r.domain, 'A,AAAA,MX');
          const hs = window.computeDomainHealthScore({domainRow: r, rootDns: dnsData});
          return window.healthGradeBadge(hs.score, hs.grade);
        }},
        {label:'Actions', html: r => {
          let acts = `<button class="btn-icon" onclick="navigate('domain-detail',${r.id})" title="View details">&#128065;</button>`;
          acts += `<button class="btn-icon" onclick="window.open('${domainUsableHttps(r) ? 'https' : 'http'}://${esc(r.domain)}','_blank')" title="Open in browser">&#8599;</button>`;
          acts += `<button class="btn-icon" onclick="copyText('${esc(r.domain)}')" title="Copy domain">&#128203;</button>`;
          if (r.can_delete !== false) {
            acts += `<button class="btn-icon" style="color:var(--red)" onclick="removeDomain('${esc(r.domain)}')" title="Remove domain">&#10005;</button>`;
          }
          return acts;
        }}
      ], rows, 'No domains');
    };
    if (lifecycle && lifecycle.setRenderTable) lifecycle.setRenderTable(render);
    else window.renderTable = render;
    p.innerHTML += `<div id="domains-table"></div>`;
    window.renderTable();

    // Progressive DNS loading: concurrency=3, one domain at a time
    const rows = domains.filter(r => {
      if (!window.searchTerm) return true;
      return (r.domain||'').toLowerCase().includes((window.searchTerm||'').toLowerCase());
    });

    const domainListTypes = 'A,AAAA,MX';
    await window.processBatch(rows, 3, async (r) => {
      if (activeDomainsLifecycle && !activeDomainsLifecycle.isActive()) return;
      if (DnsCache.get(r.domain, domainListTypes)) return;
      if (DnsCache.isLoading(r.domain, domainListTypes)) {
        await DnsCache.waitFor(r.domain, domainListTypes);
        if (activeDomainsLifecycle && !activeDomainsLifecycle.isActive()) return;
        return;
      }
      DnsCache.setLoading(r.domain, domainListTypes);
      try {
        const res = await api('/api/domains/' + encodeURIComponent(r.domain) + '/dns-check?types=' + domainListTypes);
        if (activeDomainsLifecycle && !activeDomainsLifecycle.isActive()) return;
        DnsCache.set(r.domain, domainListTypes, res.data || {});
      } catch(e) {
        DnsCache.set(r.domain, domainListTypes, null);
        return;
      }
      const idx = rows.indexOf(r);
      const row = document.querySelector(`#domains-table table tbody tr:nth-child(${idx+1})`);
      if (!row) return;
      const cells = row.querySelectorAll('td');
      if (cells.length < 9) return;
      const dnsData = DnsCache.get(r.domain);
      cells[4].innerHTML = window.dnsStatusBadge(dnsData ? dnsData.overall_status : null);
      // Health score uses HealthCache.load (full context) or shows '...'
      window.HealthCache.load(r.domain, r, null, null).then(function(healthResult) {
        if (activeDomainsLifecycle && !activeDomainsLifecycle.isActive()) return;
        if (!healthResult || healthResult.score == null) return;
        var hRow = document.querySelector(`#domains-table table tbody tr:nth-child(${idx+1})`);
        if (!hRow) return;
        var hCells = hRow.querySelectorAll('td');
        if (hCells.length < 9) return;
        hCells[8].innerHTML = window.healthGradeBadge(healthResult.score, healthResult.grade);
      });
    });

    // Progressive Runtime loading (separate pass, concurrency=3)
    const siteRows = rows.filter(r => r.site_id && r.site_id > 0);
    await window.processBatch(siteRows, 3, async (r) => {
      if (activeDomainsLifecycle && !activeDomainsLifecycle.isActive()) return;
      if (RuntimeCache.get(r.site_id)) return;
      try {
        const res = await api('/api/runtime/' + r.site_id);
        if (activeDomainsLifecycle && !activeDomainsLifecycle.isActive()) return;
        RuntimeCache.set(r.site_id, res.data || {});
      } catch(e) {
        return;
      }
      const idx = rows.indexOf(r);
      const row = document.querySelector(`#domains-table table tbody tr:nth-child(${idx+1})`);
      if (!row) return;
      const cells = row.querySelectorAll('td');
      if (cells.length < 9) return;
      const rtData = RuntimeCache.get(r.site_id);
      if (rtData) cells[6].innerHTML = window.runtimeStatusBadge(rtData.web);
    });

  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load domains</div>'; }
}

// ===== DOMAIN DETAIL =====
let _currentDomain = null;
let _domainIdForTab = null;

async function loadDomainDetail(p, domainId, lifecycle) {
  activeDomainsLifecycle = lifecycle || activeDomainsLifecycle;
  _domainIdForTab = domainId;
  try {
    // Fetch domain data (from enriched list)
    const allDomains = await api('/api/domains');
    if (lifecycle && !lifecycle.isActive()) return;
    const domainRow = (allDomains.data || []).find(d => d.id == domainId);
    if (!domainRow) { p.innerHTML = '<div class="empty-state">Domain not found</div>'; return; }
    _currentDomain = domainRow;

    // Fetch mail domain data
    let mailDomain = null;
    try {
      const mdRes = await api('/api/mail/domains');
      mailDomain = (mdRes.data || []).find(m => m.domain === domainRow.domain || (domainRow.id > 0 && m.domain_id == domainId)) || null;
    } catch(e) {
      console.error('Failed to load mail domain', e);
    }

    // Fetch server hostname and its DNS (A/AAAA records) for expected IP comparison
    let serverHostname = '';
    let serverDns = null;
    try {
      const settingsRes = await api('/api/settings');
      if (settingsRes && settingsRes.data) {
        serverHostname = settingsRes.data.server_hostname || '';
        // Resolve server_hostname's A and AAAA records as the expected IPs
        if (serverHostname) {
          const srvDnsRes = await fetchDnsForFqdn(serverHostname, 'A,AAAA');
          if (srvDnsRes) serverDns = srvDnsRes;
        }
      }
    } catch(e) {
      console.error('Failed to load settings', e);
    }

    // SSL data from enriched GET /api/domains (ssl_status, ssl_enabled)
    // No extra SSL API call — use domainRow fields directly.
    // ssl_status values: Active, Disabled, Expired, Expiring, Error, Issuing

    let runtimeData = null;
    if (domainRow.site_id > 0) {
      try {
        const rtRes = await api('/api/runtime/' + domainRow.site_id);
        if (rtRes.success) runtimeData = rtRes.data;
      } catch(e) {
        console.error('Failed to load runtime data for site ' + domainRow.site_id, e);
      }
    }

    // Health score via HealthCache.load (async — updates header in-place)
    var hsInitial = window.HealthCache.get(domainRow.domain);
    var hsDisplay = hsInitial && hsInitial !== 'loading' ? hsInitial : null;
    var hsBadge = window.healthGradeBadge(hsDisplay ? hsDisplay.score : null, hsDisplay ? hsDisplay.grade : 'N/A');

    p.innerHTML = `
      <div class="page-header">
        <h1><a href="#" onclick="navigate('domains');return false" style="color:var(--text2);text-decoration:none;">&larr;</a> ${esc(domainRow.domain)}</h1>
        <div class="page-actions">
          <span class="health-badge" style="margin-right:8px;font-size:13px;">Health: ${hsBadge}</span>
          <button class="btn btn-sm" onclick="window.open('${domainUsableHttps(domainRow) ? 'https' : 'http'}://${esc(domainRow.domain)}','_blank')">Open</button>
          <button class="btn btn-sm" onclick="copyText('${esc(domainRow.domain)}')">Copy</button>
          ${domainRow.can_delete !== false ? `<button class="btn btn-sm btn-danger" onclick="removeDomain('${esc(domainRow.domain)}')">Remove</button>` : ''}
        </div>
      </div>
      <div class="tabs" id="domain-tabs">
        <div class="tab active" data-tab="overview" onclick="switchDomainTab('overview')">Overview</div>
        <div class="tab" data-tab="dns-records" onclick="switchDomainTab('dns-records')">DNS Records</div>
        <div class="tab" data-tab="mail" onclick="switchDomainTab('mail')">Mail</div>
        <div class="tab" data-tab="security" onclick="switchDomainTab('security')">Security</div>
        <div class="tab" data-tab="health" onclick="switchDomainTab('health')">Health</div>
      </div>
      <div id="domain-tab-content"></div>`;

    // Store data for tab access
    window._domainDetailData = {domainRow, mailDomain, runtimeData, serverHostname, serverDns};

    // Async health score load — updates header badge when ready
    window.HealthCache.load(domainRow.domain, domainRow, mailDomain, serverHostname).then(function(healthResult) {
      if (!healthResult || healthResult.score == null) return;
      var badgeSpan = p.querySelector('.page-actions .health-badge');
      if (!badgeSpan) return;
      badgeSpan.innerHTML = 'Health: ' + window.healthGradeBadge(healthResult.score, healthResult.grade);
    });

    // Load first tab
    loadDomainOverview();
  } catch(e) {
    p.innerHTML = '<div class="empty-state">Failed to load domain</div>';
  }
}

function switchDomainTab(tabId) {
  document.querySelectorAll('#domain-tabs .tab').forEach(t => t.classList.toggle('active', t.dataset.tab === tabId));
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  if (tabId === 'overview') loadDomainOverview();
  else if (tabId === 'dns-records') loadDomainDnsRecords();
  else if (tabId === 'mail') loadDomainMail();
  else if (tabId === 'security') loadDomainSecurity();
  else if (tabId === 'health') loadDomainHealth();
  else content.innerHTML = '<div class="empty-state">Coming soon</div>';
}

// Helper: fetch DNS check for a specific FQDN with typed cache
// Cache key includes both FQDN and type list (e.g., 'example.com|A,TXT')
// to prevent cache collisions between different type queries for the same domain.
async function fetchDnsForFqdn(fqdn, types) {
  const cached = DnsCache.get(fqdn, types);
  if (cached) return cached;
  if (DnsCache.isLoading(fqdn, types)) return DnsCache.waitFor(fqdn, types);
  DnsCache.setLoading(fqdn, types);
  try {
    const res = await api('/api/domains/' + encodeURIComponent(fqdn) + '/dns-check?types=' + types);
    if (res && res.success && res.data) {
      DnsCache.set(fqdn, types, res.data);
      return res.data;
    }
    DnsCache.set(fqdn, types, null);
    return null;
  } catch(e) {
    console.error('DNS check failed for ' + fqdn, e);
    DnsCache.set(fqdn, types, null);
    return null;
  }
}

// --- System domain action helper ---
async function runSystemAction(btnId, actionFn, confirmMsg, onSuccess) {
  var btn = document.getElementById(btnId);
  if (!btn || btn.disabled) return;
  if (confirmMsg && !confirm(confirmMsg)) return;
  btn.disabled = true;
  btn.textContent = 'Working...';
  try {
    var res = await actionFn();
    if (res && res.success) {
      toast(res.data && res.data.message ? res.data.message : 'Action completed', 'success');
      if (onSuccess) onSuccess();
    } else {
      toast('Error: ' + (res && res.error ? res.error : 'Unknown error'), 'error');
    }
  } catch(e) {
    toast('Error: ' + (e.message || 'Request failed'), 'error');
  }
  btn.disabled = false;
  btn.textContent = btnId === 'proxy-test-btn' ? 'Test Global Proxy Config' :
                    btnId === 'proxy-reload-btn' ? 'Reload Global Proxy' :
                    btnId === 'proxy-sync-btn' ? 'Sync All Proxy Configs' :
                    btnId === 'ssl-renew-btn' ? 'Renew Certificate' :
                    btnId === 'ssl-issue-btn' ? 'Issue New Certificate' : 'Action';
}

// Fetch SSL details and populate the SSL detail card
async function loadSslDetails(domain) {
  var infoEl = document.getElementById('ssl-detail-info');
  if (!infoEl) return;
  try {
    var res = await api('/api/ssl/' + encodeURIComponent(domain));
    if (res && res.success && res.data) {
      var d = res.data;
      infoEl.innerHTML = '';
      if (d.issuer) infoEl.innerHTML += '<div>Issuer: ' + esc(d.issuer) + '</div>';
      if (d.expires_at) infoEl.innerHTML += '<div>Expires: ' + esc(d.expires_at) + '</div>';
      if (d.domains && d.domains.length) infoEl.innerHTML += '<div>Domains: ' + esc(d.domains.join(', ')) + '</div>';
      if (d.status === 'active' && d.expires_at) {
        var days = Math.round((new Date(d.expires_at) - new Date()) / 86400000);
        if (days > 0) infoEl.innerHTML += '<div>Days remaining: ' + days + '</div>';
      }
    } else {
      infoEl.textContent = 'Certificate details not available';
    }
  } catch(e) {
    infoEl.textContent = 'Failed to load certificate details';
  }
}

// --- Overview tab ---
async function loadDomainOverview() {
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  const dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }
  const {domainRow, mailDomain, runtimeData, serverHostname, serverDns} = dd;
  const domain = domainRow.domain;

  content.innerHTML = '<div class="empty-state">Checking DNS...</div>';

  // Helper: format a record value for display (truncate if too long)
  function fmtVal(v) {
    if (!v || typeof v !== 'string') return '—';
    return v.length > 40 ? v.substr(0, 40) + '...' : v;
  }

  // Helper: get records from a dnsResult for a specific type
  function getRecs(dnsResult, typeName) {
    if (!dnsResult || !Array.isArray(dnsResult.per_type)) return [];
    const pt = dnsResult.per_type.find(x => x && x.type === typeName);
    if (!pt || !Array.isArray(pt.records)) return [];
    return pt.records;
  }

  // Fetch DNS data for each FQDN separately
  // Root domain: A, AAAA, MX, TXT (for SPF)
  const rootDns = await fetchDnsForFqdn(domain, 'A,AAAA,MX,TXT');

  // DKIM: query <selector>._domainkey.<domain> (separate FQDN)
  let dkimDns = null;
  if (mailDomain && mailDomain.dkim_public_key_dns) {
    const selector = mailDomain.dkim_selector || 'dkim';
    const dkimFqdn = selector + '._domainkey.' + domain;
    dkimDns = await fetchDnsForFqdn(dkimFqdn, 'TXT');
  }

  // DMARC: query _dmarc.<domain> (separate FQDN)
  let dmarcDns = null;
  if (mailDomain) {
    const dmarcFqdn = '_dmarc.' + domain;
    dmarcDns = await fetchDnsForFqdn(dmarcFqdn, 'TXT');
  }

  // Determine expected MX target based on MailDomain mode
  let expectedMx = window.getExpectedMxTarget(mailDomain, serverHostname);

  // Build DNS check summary table
  const expectedTypes = ['A', 'AAAA', 'MX'];
  const mailActive = mailDomain && mailDomain.mode !== 'disabled';
  if (mailActive) {
    expectedTypes.push('SPF');
    if (mailDomain.dkim_public_key_dns) expectedTypes.push('DKIM');
    expectedTypes.push('DMARC');
  }

  let dnsRows = '';
  for (const type of expectedTypes) {
    let configured = '';
    let published = '';
    let statusCls = 'badge-info';
    let statusLabel = '';
    let recs = [];
    let hasExpected = false;

    if (type === 'A') {
      recs = getRecs(rootDns, 'A');
      // Expected IPv4 from NetworkService (auto-detected, via API)
      configured = rootDns && typeof rootDns.expected_ipv4 === 'string' && rootDns.expected_ipv4.length > 0
        ? rootDns.expected_ipv4 : '';
      hasExpected = !!configured;
    } else if (type === 'AAAA') {
      recs = getRecs(rootDns, 'AAAA');
      // Expected IPv6 from NetworkService (auto-detected, via API)
      configured = rootDns && typeof rootDns.expected_ipv6 === 'string' && rootDns.expected_ipv6.length > 0
        ? rootDns.expected_ipv6 : '';
      hasExpected = !!configured;
    } else if (type === 'MX') {
      recs = getRecs(rootDns, 'MX');
      configured = expectedMx;
      hasExpected = !!configured;
    } else if (type === 'SPF') {
      recs = getRecs(rootDns, 'TXT').filter(r => typeof r.value === 'string' && r.value.startsWith('v=spf1'));
      configured = mailActive ? 'v=spf1 mx ~all' : '';
      hasExpected = !!configured;
    } else if (type === 'DKIM') {
      recs = getRecs(dkimDns, 'TXT');
      configured = mailDomain && mailDomain.dkim_public_key_dns ? mailDomain.dkim_public_key_dns : '';
      hasExpected = !!configured;
    } else if (type === 'DMARC') {
      recs = getRecs(dmarcDns, 'TXT');
      configured = mailActive ? 'v=DMARC1; p=none;' : '';
      hasExpected = !!configured;
    }

    if (recs.length > 0) {
      if (type === 'MX') {
        // MX: show hostname primarily, priority as secondary
        published = recs.map(r => fmtVal(r.value) + (r.priority ? ' (priority ' + r.priority + ')' : '')).join(', ');
      } else {
        published = recs.map(r => fmtVal(r.value)).join(', ');
      }
    }

    // Determine column label and status
    // For records where ContainerCP has a configured/stored value → "Configured"
    // For records where ContainerCP generates a recommendation → "Recommended"
    const colLabel = (type === 'SPF' || type === 'DMARC') ? 'Recommended' : 'Configured';
    const displayVal = configured ? fmtVal(configured) : '—';
    const displayPub = published || '—';

    if (hasExpected && published || hasExpected && !published || !hasExpected && published) {
      if (type === 'A') {
        const r = window.compareIpRecords(recs, configured);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'AAAA') {
        const r = window.compareIpRecords(recs, configured);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'MX') {
        const r = window.compareMxRecords(recs, expectedMx);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'SPF') {
        const r = window.compareSpfRecords(configured, published, rootDns && rootDns.spf_analysis);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'DKIM') {
        const pubVal = recs.length > 0 ? recs[0].value : '';
        const r = window.compareDkimRecords(configured, pubVal);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'DMARC') {
        const pubVal = recs.length > 0 ? recs[0].value : '';
        const r = window.compareDmarcRecords(configured, pubVal);
        statusLabel = r.status; statusCls = r.cls;
      } else {
        statusLabel = published ? 'Match' : 'N/A'; statusCls = published ? 'badge-ok' : 'badge-info';
      }
    }

    // Build column header: show "Recommended" for SPF/DMARC, "Configured" for others
    dnsRows += `<tr><td>${esc(type)}</td><td style="font-family:monospace;font-size:12px;">${esc(displayVal)}</td><td style="font-family:monospace;font-size:12px;">${esc(displayPub)}</td><td>${statusLabel ? '<span class="badge ' + statusCls + '">' + esc(statusLabel) + '</span>' : '<span class="badge badge-info">—</span>'}</td></tr>`;
  }

  // Build info cards
  const mailCard = mailDomain ? `
    <div class="card" style="cursor:pointer;" onclick="switchDomainTab('mail')">
      <h3>Mail</h3>
      <div style="margin-top:8px;font-size:13px;">
        <div>Domain: <strong>${esc(mailDomain.domain)}</strong></div>
        <div>Mode: <span class="badge badge-info">${esc(mailDomain.mode)}</span></div>
        <div>DKIM: ${mailDomain.dkim_public_key_dns ? '<span class="badge badge-ok">Generated</span>' : '<span class="badge badge-info">Not generated</span>'}</div>
      </div>
    </div>` : `
    <div class="card">
      <h3>Mail</h3>
      <div style="margin-top:8px;font-size:13px;color:var(--text3);">Not configured</div>
    </div>`;

  // SSL card: ssl_status as single source of truth. Both lines use badge components.
  const sslStatusDisplay = domainRow.ssl_status || 'Unknown';
  const sslBadgeCls = {'active':'badge-ok','http_only':'badge-info','disabled':'badge-info','expiring':'badge-warn','expired':'badge-err','error':'badge-err','issuing':'badge-warn'};
  const sslBadgeMap = {'active':'Active','http_only':'HTTP Only','disabled':'Disabled','expiring':'Expiring','expired':'Expired','error':'Error','issuing':'Issuing'};
  const sslKey = sslStatusDisplay.toLowerCase();
  const httpsBadge = (domainRow.ssl_enabled || sslStatusDisplay === 'Active' || sslStatusDisplay === 'Expiring') ? 'badge-ok' : 'badge-err';
  const httpsLabel = (domainRow.ssl_enabled || sslStatusDisplay === 'Active' || sslStatusDisplay === 'Expiring') ? 'Active' : 'Inactive';
  const sslCard = `
    <div class="card">
      <h3>SSL Certificate</h3>
      <div style="margin-top:8px;font-size:13px;">
        <div>Status: <span class="badge ${sslBadgeCls[sslKey] || 'badge-info'}">${esc(sslBadgeMap[sslKey] || sslStatusDisplay)}</span></div>
        <div>HTTPS: <span class="badge ${httpsBadge}">${httpsLabel}</span></div>
      </div>
    </div>`;

  const siteCard = domainRow.site_name ? `
    <div class="card" ${domainRow.site_id > 0 ? `onclick="navigate('site-detail',${domainRow.site_id})" style="cursor:pointer;"` : ''}>
      <h3>Site</h3>
      <div style="margin-top:8px;font-size:13px;">
        <div>Name: <strong>${esc(domainRow.site_name)}</strong></div>
        <div>Domain: ${esc(domainRow.site_domain || '')}</div>
        <div>Runtime: ${runtimeData ? window.runtimeStatusBadge(runtimeData.web) : '<span class="badge badge-info">N/A</span>'}</div>
      </div>
    </div>` : '';

  content.innerHTML = `
    <div style="margin-bottom:12px;">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
        <h3 style="font-size:14px;">DNS Check</h3>
        <button class="btn btn-sm" onclick="refreshDomainOverview()">Check Again</button>
      </div>
      <div class="table-wrap">
        <table>
          <thead><tr><th>Type</th><th>Configured</th><th>Published</th><th>Status</th></tr></thead>
          <tbody>${dnsRows}</tbody>
        </table>
      </div>
    </div>
    <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px;">
      ${mailCard}
      ${sslCard}
      ${siteCard}
      ${domainRow.system_role === 'admin-panel' ? `
      <div class="card">
        <h3>System: Admin Panel</h3>
        <div style="margin-top:8px;font-size:13px;">
          <div>Site ID: <code>${domainRow.site_id}</code></div>
          <div>Role: <span class="badge badge-admin">${esc(domainRow.system_role)}</span></div>
          <div>FQDN: <strong>${esc(domainRow.domain)}</strong></div>
          <div>Proxy upstream: <code>${esc(domainRow.proxy_upstream || '—')}</code></div>
          <div>SSL: <span class="badge ${sslBadgeCls[sslKey] || 'badge-info'}">${esc(sslBadgeMap[sslKey] || sslStatusDisplay)}</span></div>
          <div>Runtime: <span class="badge badge-info">N/A</span></div>
        </div>
      </div>` : ''}
      ${domainRow.can_manage_proxy ? `
      <div class="card">
        <h3>Proxy Configuration</h3>
        <div style="margin-top:8px;font-size:13px;">
          <div>Upstream: <code>${esc(domainRow.proxy_upstream || '—')}</code></div>
          <div>Status: <span class="badge badge-info">${domainRow.proxy_upstream ? 'Available' : 'Not verified'}</span></div>
          <div style="margin-top:8px;font-size:11px;color:var(--text3);">
            These operations affect the central reverse proxy for ALL domains.
            An invalid configuration may interrupt access to ContainerCP and other sites.
          </div>
          <div style="margin-top:8px;">
            <button class="btn btn-sm" id="proxy-test-btn">Test Global Proxy Config</button>
            <button class="btn btn-sm" id="proxy-reload-btn" style="margin-left:4px;">Reload Global Proxy</button>
            <button class="btn btn-sm" id="proxy-sync-btn" style="margin-left:4px;">Sync All Proxy Configs</button>
          </div>
        </div>
      </div>` : ''}
      ${domainRow.can_manage_ssl ? `
      <div class="card" id="ssl-detail-card">
        <h3>SSL Certificate</h3>
        <div style="margin-top:8px;font-size:13px;">
          <div>Status: <span class="badge ${sslBadgeCls[sslKey] || 'badge-info'}">${esc(sslBadgeMap[sslKey] || sslStatusDisplay)}</span></div>
          <div>HTTPS: <span class="badge ${httpsBadge}">${httpsLabel}</span></div>
          <div style="margin-top:4px;font-size:11px;color:var(--text3);" id="ssl-detail-info">Loading details...</div>
          <div style="margin-top:8px;font-size:11px;color:var(--text3);">
            ACME rate limits apply. Temporary HTTPS interruption may occur during issuance.
          </div>
          <div style="margin-top:8px;">
            ${sslStatusDisplay === 'Active' || sslStatusDisplay === 'Expiring' ? `<button class="btn btn-sm" id="ssl-renew-btn">Renew Certificate</button>` : ''}
            ${sslStatusDisplay === 'Disabled' || sslStatusDisplay === 'Error' || sslStatusDisplay === '' || sslStatusDisplay === 'HTTP_ONLY' || sslStatusDisplay === 'http_only' ? `<button class="btn btn-sm" id="ssl-issue-btn">Issue New Certificate</button>` : ''}
            ${sslStatusDisplay === 'Issuing' ? `<span class="badge badge-warn">Issuance in progress</span>` : ''}
          </div>
        </div>
      </div>` : ''}
    </div>`;

  // Wire system-domain action buttons via single onclick handler (no accumulation)
  // Using onclick = assignment replaces previous handler, preventing duplicates
  var tabContent = document.getElementById('domain-tab-content');
  if (tabContent) {
    var handler = function(e) {
      var target = e.target;
      if (target.id === 'proxy-test-btn')
        runSystemAction('proxy-test-btn', function() { return apiPost('/api/proxy/test'); }, null);
      else if (target.id === 'proxy-reload-btn')
        runSystemAction('proxy-reload-btn', function() { return apiPost('/api/proxy/reload'); },
          'Reload central reverse proxy?\n\nThis will restart nginx for ALL domains. An invalid configuration may interrupt access to ContainerCP and other sites.');
      else if (target.id === 'proxy-sync-btn')
        runSystemAction('proxy-sync-btn', function() { return apiPost('/api/proxy/sync'); },
          'Synchronize all proxy configurations?\n\nThis regenerates HTTPS proxy configs for ALL domains.');
      else if (target.id === 'ssl-renew-btn')
        runSystemAction('ssl-renew-btn', function() { return apiPost('/api/ssl/' + encodeURIComponent(domainRow.domain) + '/renew'); },
          'Renew SSL certificate for ' + domainRow.domain + '?\n\nACME rate limits apply. Temporary HTTPS interruption may occur.',
          function() { loadSslDetails(domainRow.domain); refreshDomainOverview(); });
      else if (target.id === 'ssl-issue-btn')
        runSystemAction('ssl-issue-btn', function() { return apiPost('/api/ssl/' + encodeURIComponent(domainRow.domain) + '/issue', {provider_id:'letsencrypt'}); },
          'Issue new SSL certificate for ' + domainRow.domain + '?\n\nACME rate limits apply. Temporary HTTPS interruption may occur.',
          function() { loadSslDetails(domainRow.domain); refreshDomainOverview(); });
    };
    tabContent.onclick = handler;
  }

  // Load SSL details for the admin panel
  if (domainRow.can_manage_ssl && domainRow.system_role === 'admin-panel') {
    loadSslDetails(domainRow.domain);
  }
}

async function refreshDomainOverview() {
  if (!_currentDomain) return;
  DnsCache.clear(_currentDomain.domain);  // clears ALL type variants for this domain
  loadDomainOverview();
}

// Universal Full DNS Record formatter// Normalize hostname for comparison: lowercase, strip trailing dot// Attach data-copy event listener (single handler for all copy buttons)// --- DNS Records tab ---
async function loadDomainDnsRecords() {
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  const dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }
  const {domainRow, mailDomain, serverHostname} = dd;
  const domain = domainRow.domain;

  content.innerHTML = '<div class="empty-state">Checking DNS...</div>';

  function getRecs(dnsResult, typeName) {
    if (!dnsResult || !Array.isArray(dnsResult.per_type)) return [];
    const pt = dnsResult.per_type.find(x => x && x.type === typeName);
    if (!pt || !Array.isArray(pt.records)) return [];
    return pt.records;
  }

  const rootDns = await fetchDnsForFqdn(domain, 'A,AAAA,MX,TXT,NS,CNAME,CAA');
  let dkimDns = null, dmarcDns = null, mtaStsDns = null;

  if (mailDomain && mailDomain.dkim_public_key_dns) {
    const sel = mailDomain.dkim_selector || 'dkim';
    dkimDns = await fetchDnsForFqdn(sel + '._domainkey.' + domain, 'TXT');
  }
  if (mailDomain) {
    dmarcDns = await fetchDnsForFqdn('_dmarc.' + domain, 'TXT');
    mtaStsDns = await fetchDnsForFqdn('_mta-sts.' + domain, 'TXT');
  }

  let expectedMx = '';
  expectedMx = window.getExpectedMxTarget(mailDomain, serverHostname);

  const now = Date.now();
  const ts = new Date(now).toLocaleTimeString();

  function fmtVal(v, max) {
    if (!v || typeof v !== 'string') return '—';
    if (v.length > (max || 40)) return esc(v.substr(0, max || 40)) + '...';
    return esc(v);
  }

  function statusBadge(label, cls) {
    if (!label) return '<span class="badge badge-info">—</span>';
    return `<span class="badge ${cls || 'badge-info'}">${esc(label)}</span>`;
  }

  // Data-copy buttons: safe pattern with attribute-based copying
  function copyBtn(text, shortLabel, fullLabel) {
    const safe = text.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
    return `<button class="btn-icon" data-copy="${safe}" title="${esc(fullLabel || shortLabel)}">${esc(shortLabel)}</button>`;
  }

  function copyRowButtons(record) {
    const {host, type, value, ttl, priority, domainName} = record;
    const name = host === '@' ? domainName : host;
    const fqdn = name.endsWith('.') ? name : name + '.';
    const fullRecord = buildFullDnsRecord(name, ttl, type, value, priority);
    return `<span style="white-space:nowrap;">
      ${copyBtn(name, 'H', 'Copy Host')}
      ${copyBtn(value, 'V', 'Copy Value')}
      ${copyBtn(fqdn, 'F', 'Copy FQDN')}
      ${copyBtn(fullRecord, 'R', 'Copy Full Record')}
    </span>`;
  }

  let rows = '';

  // 1. A record
  {
    const recs = getRecs(rootDns, 'A');
    const expected = rootDns && rootDns.expected_ipv4 || '';
    const publishedIps = recs.map(r => r.value).filter(Boolean);
    const published = publishedIps.join(', ');
    const aR = window.compareIpRecords(recs, expected);
    const ttl = recs.length > 0 ? recs[0].ttl : 0;
    const valToCopy = publishedIps[0] || expected;
    rows += `<tr>
      <td>${statusBadge(aR.status, aR.cls)}</td>
      <td>A</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${fmtVal(expected)}</td>
      <td style="font-family:monospace;">${fmtVal(published)}</td>
      <td>${ttl || '—'}</td>
      <td>${valToCopy ? copyRowButtons({host:'@', type:'A', value:valToCopy, ttl, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 2. AAAA
  {
    const recs = getRecs(rootDns, 'AAAA');
    const expected = rootDns && typeof rootDns.expected_ipv6 === 'string' ? rootDns.expected_ipv6 : '';
    const aaaaR = window.compareIpRecords(recs, expected);
    const publishedIps = recs.map(r => r.value).filter(Boolean);
    const published = publishedIps.join(', ');
    const ttl = recs.length > 0 ? recs[0].ttl : 0;
    const valToCopy = publishedIps[0] || expected;
    rows += `<tr>
      <td>${statusBadge(aaaaR.status, aaaaR.cls)}</td>
      <td>AAAA</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${fmtVal(expected)}</td>
      <td style="font-family:monospace;">${fmtVal(published)}</td>
      <td>${ttl || '—'}</td>
      <td>${valToCopy ? copyRowButtons({host:'@', type:'AAAA', value:valToCopy, ttl, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 3. MX
  {
    const recs = getRecs(rootDns, 'MX');
    const configured = expectedMx || '';
    const r = window.compareMxRecords(recs, configured);
    const publishedStr = window.formatMxPublished(recs);
    rows += `<tr>
      <td>${statusBadge(r.status, r.cls)}</td>
      <td>MX</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${fmtVal(configured)}</td>
      <td style="font-family:monospace;">${fmtVal(publishedStr)}</td>
      <td>—</td>
      <td>${recs.length > 0 ? copyRowButtons({host:'@', type:'MX', value:recs[0].value || configured, ttl:recs[0].ttl, priority:recs[0].priority, domainName:domain}) : configured ? copyRowButtons({host:'@', type:'MX', value:configured, ttl:3600, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 4. SPF
  {
    const recs = getRecs(rootDns, 'TXT').filter(r => typeof r.value === 'string' && r.value.startsWith('v=spf1'));
    const recommended = mailDomain ? 'v=spf1 mx ~all' : '';
    const publishedVal = recs.length > 0 ? recs[0].value : '';
    const r = window.compareSpfRecords(recommended, publishedVal, rootDns && rootDns.spf_analysis);
    const val = publishedVal || recommended;
    rows += `<tr>
      <td>${statusBadge(r.status, r.cls)}</td>
      <td>SPF</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${recommended ? esc(recommended) : '—'}</td>
      <td style="font-family:monospace;">${fmtVal(publishedVal)}</td>
      <td>${recs.length > 0 ? recs[0].ttl : '—'}</td>
      <td>${val ? copyRowButtons({host:'@', type:'TXT', value:val, ttl:recs.length > 0 ? recs[0].ttl : 3600, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 5. DKIM
  if (dkimDns) {
    const recs = getRecs(dkimDns, 'TXT');
    const selector = mailDomain ? mailDomain.dkim_selector || 'dkim' : 'dkim';
    const host = selector + '._domainkey.' + domain;
    const pubKey = mailDomain ? mailDomain.dkim_public_key_dns || '' : '';
    const publishedVal = recs.length > 0 ? recs[0].value : '';
    const dkimR = window.compareDkimRecords(pubKey, publishedVal);
    const ttl = recs.length > 0 ? recs[0].ttl : 0;
    const val = publishedVal || pubKey;
    rows += `<tr>
      <td>${statusBadge(dkimR.status, dkimR.cls)}</td>
      <td>DKIM</td>
      <td style="font-family:monospace;">${esc(host)}</td>
      <td style="font-family:monospace;">${pubKey ? fmtVal(pubKey, 60) : '—'}</td>
      <td style="font-family:monospace;">${publishedVal ? fmtVal(publishedVal, 60) : '—'}</td>
      <td>${ttl || '—'}</td>
      <td>${val ? copyRowButtons({host, type:'TXT', value:val, ttl: ttl || 3600, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 6. DMARC
  if (dmarcDns) {
    const recs = getRecs(dmarcDns, 'TXT');
    const recommended = 'v=DMARC1; p=none;';
    const host = '_dmarc.' + domain;
    const publishedVal = recs.length > 0 ? recs[0].value : '';
    const dmarcR = window.compareDmarcRecords(recommended, publishedVal);
    const dmarcVal = publishedVal || recommended;
    rows += `<tr>
      <td>${statusBadge(dmarcR.status, dmarcR.cls)}</td>
      <td>DMARC</td>
      <td style="font-family:monospace;">${esc(host)}</td>
      <td style="font-family:monospace;">${esc(recommended)}</td>
      <td style="font-family:monospace;">${fmtVal(publishedVal)}</td>
      <td>${recs.length > 0 ? recs[0].ttl : '—'}</td>
      <td>${dmarcVal ? copyRowButtons({host, type:'TXT', value:dmarcVal, ttl: recs.length > 0 ? recs[0].ttl : 3600, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 7. CAA
  {
    const recs = getRecs(rootDns, 'CAA');
    const recommended = '0 issue "letsencrypt.org"';
    const hasPublished = recs.length > 0;
    let status, cls;
    if (hasPublished) {
      const hasRequired = recs.some(r => {
        const v = (r.value || '').replace(/\s+/g, '');
        const rn = recommended.replace(/\s+/g, '');
        return v === rn;
      });
      status = hasRequired ? 'Match' : 'Mismatch';
      cls = hasRequired ? 'badge-ok' : 'badge-warn';
    } else { status = 'Not Published'; cls = 'badge-err'; }
    const publishedStr = recs.map(r => r.value).join(', ');
    rows += `<tr>
      <td>${statusBadge(status, cls)}</td>
      <td>CAA</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${esc(recommended)}</td>
      <td style="font-family:monospace;">${fmtVal(publishedStr)}</td>
      <td>—</td>
      <td>${copyRowButtons({host:'@', type:'CAA', value:recommended, ttl:3600, domainName:domain})}</td>
    </tr>`;
  }

  // 8. NS (informational)
  {
    const recs = getRecs(rootDns, 'NS');
    const hasPublished = recs.length > 0;
    const publishedStr = recs.map(r => r.value).join(', ');
    const val = recs[0] ? recs[0].value : '';
    rows += `<tr>
      <td>${statusBadge(hasPublished ? 'Found' : 'N/A', hasPublished ? 'badge-ok' : 'badge-info')}</td>
      <td>NS</td>
      <td style="font-family:monospace;">@</td>
      <td>—</td>
      <td style="font-family:monospace;">${fmtVal(publishedStr)}</td>
      <td>${recs.length > 0 ? recs[0].ttl : '—'}</td>
      <td>${val ? copyRowButtons({host:'@', type:'NS', value:val, ttl:recs[0].ttl, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 9. MTA-STS (informational if no expected value)
  if (mtaStsDns) {
    const recs = getRecs(mtaStsDns, 'TXT');
    const hasPublished = recs.length > 0;
    const val = hasPublished ? recs[0].value : '';
    const host = '_mta-sts.' + domain;
    rows += `<tr>
      <td>${statusBadge(hasPublished ? 'Found' : 'N/A', hasPublished ? 'badge-ok' : 'badge-info')}</td>
      <td>MTA-STS</td>
      <td style="font-family:monospace;">${esc(host)}</td>
      <td>—</td>
      <td style="font-family:monospace;">${fmtVal(val)}</td>
      <td>${recs.length > 0 ? recs[0].ttl : '—'}</td>
      <td>${val ? copyRowButtons({host, type:'TXT', value:val, ttl:recs[0].ttl, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  content.innerHTML = `
    <div id="dns-records-content">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
        <h3 style="font-size:14px;">DNS Records</h3>
        <button class="btn btn-sm" onclick="refreshDnsRecordsTab()">Check Again</button>
      </div>
      <div class="table-wrap">
        <table>
          <thead><tr><th>Status</th><th>Type</th><th>Name</th><th>Configured</th><th>Published</th><th>TTL</th><th>Actions</th></tr></thead>
          <tbody>${rows}</tbody>
        </table>
      </div>
      <div style="margin-top:8px;font-size:11px;color:var(--text3);text-align:right;">Last checked: ${ts}</div>
    </div>`;

  // Attach single data-copy event listener
  attachDataCopyListener('dns-records-content');
}

async function refreshDnsRecordsTab() {
  // Clear cache for all domain-related FQDNs
  const dd = window._domainDetailData;
  if (!dd) return;
  const domain = dd.domainRow.domain;
  DnsCache.clear(domain);
  DnsCache.clear('_dmarc.' + domain);
  DnsCache.clear('_mta-sts.' + domain);
  if (dd.mailDomain && dd.mailDomain.dkim_public_key_dns) {
    const sel = dd.mailDomain.dkim_selector || 'dkim';
    DnsCache.clear(sel + '._domainkey.' + domain);
  }
  loadDomainDnsRecords();
}
// --- Mail tab ---
async function loadDomainMail() {
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  const dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }
  const {domainRow, mailDomain, serverHostname} = dd;
  const domain = domainRow.domain;

  content.innerHTML = '<div class="empty-state">Loading...</div>';

  // Scenario B — No MailDomain
  if (!mailDomain) {
    content.innerHTML = `
      <div class="card">
        <h3>Mail</h3>
        <div style="margin-top:8px;font-size:13px;color:var(--text3);">
          <p>Mail service is not configured for this domain.</p>
          <p>MX, SPF, DKIM, DMARC are not shown as errors because mail is not in use.</p>
        </div>
        <div style="margin-top:12px;">
          <button class="btn btn-sm btn-primary" onclick="navigate('mail')">Enable Mail for this Domain</button>
        </div>
      </div>`;
    return;
  }

  // Scenario A — MailDomain exists

  // Fetch mailboxes/aliases count
  let mailboxCount = 0, aliasCount = 0;
  try {
    const mb = await api('/api/mail/domains/' + mailDomain.id + '/mailboxes');
    if (mb && mb.data) mailboxCount = mb.data.length;
  } catch(e) { console.error('Failed to load mailboxes', e); }
  try {
    const al = await api('/api/mail/domains/' + mailDomain.id + '/aliases');
    if (al && al.data) aliasCount = al.data.length;
  } catch(e) { console.error('Failed to load aliases', e); }

  // Fetch DNS data for Required and Recommended records
  // Query A+TXT together to get both TXT records and expected_ipv4 from NetworkService
  const rootDns = await fetchDnsForFqdn(domain, 'A,TXT');
  const rootMx = await fetchDnsForFqdn(domain, 'MX');
  const rootCaa = await fetchDnsForFqdn(domain, 'CAA');
  const expectedIpv4 = rootDns && typeof rootDns.expected_ipv4 === 'string' ? rootDns.expected_ipv4 : '';

  let dkimDns = null, dmarcDns = null, mtaStsDns = null, tlsRptDns = null, autoDiscoverDns = null;
  if (mailDomain.dkim_public_key_dns) {
    const sel = mailDomain.dkim_selector || 'dkim';
    dkimDns = await fetchDnsForFqdn(sel + '._domainkey.' + domain, 'TXT');
  }
  dmarcDns = await fetchDnsForFqdn('_dmarc.' + domain, 'TXT');
  mtaStsDns = await fetchDnsForFqdn('_mta-sts.' + domain, 'TXT');
  tlsRptDns = await fetchDnsForFqdn('_smtp._tls.' + domain, 'TXT');
  if (serverHostname) {
    autoDiscoverDns = await fetchDnsForFqdn('autodiscover.' + domain, 'CNAME,A');
  }

  // Determine expected MX
  let expectedMx = '';
  expectedMx = window.getExpectedMxTarget(mailDomain, serverHostname);

  // === Required Records ===
  const mxPubRecs = window.getDnsRecs(rootMx, 'MX');
  const mxPublished = window.formatMxPublished(mxPubRecs);
  const mxNorm = window.normalizeHostname(expectedMx);
  const mxMatch = mxPubRecs.some(r => window.normalizeHostname(r.value) === mxNorm);
  const mxStatus = window.computeRecordStatus(expectedMx, mxPublished, () => mxMatch);

  const spfRecs = window.getDnsRecs(rootDns, 'TXT').filter(r => typeof r.value === 'string' && r.value.startsWith('v=spf1'));
  const spfRecommended = 'v=spf1 mx ~all';
  const spfPublished = spfRecs.length > 0 ? spfRecs[0].value : '';
  const spfStatus = window.compareSpfRecords(spfRecommended, spfPublished, rootDns && rootDns.spf_analysis);

  const dkimRecs = dkimDns ? window.getDnsRecs(dkimDns, 'TXT') : [];
  const dkimKey = mailDomain.dkim_public_key_dns || '';
  const dkimPublished = dkimRecs.length > 0 ? dkimRecs[0].value : '';
  const dkimStatus = window.computeRecordStatus(dkimKey, dkimPublished, (a,b) => window.normalizeDnsValue(a) === window.normalizeDnsValue(b));

  const dmarcRecs = dmarcDns ? window.getDnsRecs(dmarcDns, 'TXT') : [];
  const dmarcRecommended = 'v=DMARC1; p=none;';
  const dmarcPublished = dmarcRecs.length > 0 ? dmarcRecs[0].value : '';
  const dmarcStatus = window.computeRecordStatus(dmarcRecommended, dmarcPublished, (a,b) => window.normalizeDmarcValue(a) === window.normalizeDmarcValue(b));

  // === Recommended Records ===
  // Autodiscover: standard is CNAME autodiscover.<domain> → <server_hostname>
  // or A record pointing to the server's public IP. Query both.
  const autoCnameRecs = autoDiscoverDns ? window.getDnsRecs(autoDiscoverDns, 'CNAME') : [];
  const autoARecs = autoDiscoverDns ? window.getDnsRecs(autoDiscoverDns, 'A') : [];
  let autoPublished = '', autoStatus, autoMatchFn;
  const autoRecommended = serverHostname || '';
  if (autoCnameRecs.length > 0) {
    autoPublished = 'CNAME ' + autoCnameRecs[0].value;
    const normPub = window.normalizeHostname(autoCnameRecs[0].value);
    const normExp = window.normalizeHostname(serverHostname);
    autoMatchFn = () => normPub === normExp;
  } else if (autoARecs.length > 0) {
    autoPublished = autoARecs.map(r => r.value).join(', ');
    autoMatchFn = () => expectedIpv4 && autoARecs.some(r => r.value === expectedIpv4);
  }
  autoStatus = window.computeRecordStatus(autoRecommended, autoPublished, autoMatchFn);
  if (!serverHostname) autoStatus = {status: 'N/A', cls: 'badge-info'};
  const autoHost = 'autodiscover.' + domain;

  // MTA-STS
  const mtaRecs = mtaStsDns ? window.getDnsRecs(mtaStsDns, 'TXT') : [];
  const mtaRecommended = 'v=STSv1; id=1';
  const mtaPublished = mtaRecs.length > 0 ? mtaRecs[0].value : '';
  const mtaStatus = window.computeRecordStatus(mtaRecommended, mtaPublished, (a,b) => window.normalizeDnsValue(a) === window.normalizeDnsValue(b));

  // TLS-RPT
  const tlsRecs = tlsRptDns ? window.getDnsRecs(tlsRptDns, 'TXT') : [];
  const tlsRecommended = 'v=TLSRPTv1; rua=mailto:tlsrpt@' + domain;
  const tlsPublished = tlsRecs.length > 0 ? tlsRecs[0].value : '';
  const tlsStatus = window.computeRecordStatus(tlsRecommended, tlsPublished, (a,b) => window.normalizeDnsValue(a) === window.normalizeDnsValue(b));

  // CAA
  const caaRecs = rootCaa ? window.getDnsRecs(rootCaa, 'CAA') : [];
  const caaRecommended = '0 issue "letsencrypt.org"';
  const caaPublished = caaRecs.map(r => r.value).join(', ');
  const caaHasRequired = caaRecs.some(r => window.normalizeDnsValue(r.value) === window.normalizeDnsValue(caaRecommended));
  const caaStatus = caaRecs.length > 0
    ? (caaHasRequired ? {status:'Match', cls:'badge-ok'} : {status:'Mismatch', cls:'badge-warn'})
    : {status:'Not Published', cls:'badge-err'};

  // PHP Mail status (site_id > 0 only)
  let phpMailHtml = '';
  if (domainRow.site_id && domainRow.site_id > 0) {
    try {
      const ms = await api('/api/sites/' + domainRow.site_id + '/mail-status');
      if (ms && ms.data) {
        const s = ms.data;
        const phpOk = s.enabled && s.credential_exists && s.msmtprc && s.network;
        phpMailHtml = `
          <div class="card" style="margin-top:12px;">
            <h3>PHP Mail</h3>
            <div style="margin-top:8px;font-size:13px;">
              <div>Status: ${window.statusBadge(phpOk ? 'Enabled' : s.enabled ? 'Degraded' : 'Disabled', phpOk ? 'badge-ok' : s.enabled ? 'badge-warn' : 'badge-info')}</div>
              <div style="display:grid;grid-template-columns:1fr 1fr;gap:4px;margin-top:6px;font-size:12px;color:var(--text3);">
                <div>Mail Domain: ${s.mail_domain ? '✅' : '❌'}</div>
                <div>Credentials: ${s.credential_exists ? '✅' : '❌'}</div>
                <div>msmtprc: ${s.msmtprc ? '✅' : '❌'}</div>
                <div>Network: ${s.network ? '✅' : '❌'}</div>
              </div>
            </div>
          </div>`;
      }
    } catch(e) { console.error('Failed to load PHP Mail status', e); }
  }

  const dkimSelector = mailDomain.dkim_selector || 'dkim';
  const dkimHost = dkimSelector + '._domainkey.' + domain;
  const ts = new Date().toLocaleTimeString();

  content.innerHTML = `
    <div id="mail-tab-content">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
        <h3 style="font-size:14px;">Mail Configuration</h3>
        <button class="btn btn-sm" onclick="refreshMailTab()">Check Again</button>
      </div>

      <div class="card" style="margin-bottom:12px;">
        <h3>Mail Domain: ${esc(mailDomain.domain || domain)}</h3>
        <div style="margin-top:8px;font-size:13px;">
          <div>Mode: ${window.statusBadge(mailDomain.mode, 'badge-info')}</div>
          <div>Status: ${mailDomain.enabled ? window.statusBadge('Active', 'badge-ok') : window.statusBadge('Disabled', 'badge-err')}</div>
          <div>Mailboxes: <strong>${mailboxCount}</strong> &nbsp;|&nbsp; Aliases: <strong>${aliasCount}</strong></div>
        </div>
      </div>

      <h3 style="font-size:13px;margin-bottom:8px;">Required Records</h3>
      <div class="table-wrap" style="margin-bottom:12px;">
        <table>
          <thead><tr><th>Type</th><th>Configured</th><th>Published</th><th>Status</th><th>Actions</th></tr></thead>
          <tbody>
            <tr><td>MX</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(expectedMx)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(mxPublished)}</td><td>${window.statusBadge(mxStatus.status, mxStatus.cls)}</td><td>${expectedMx ? window.copyRowButtons({host:'@', type:'MX', value:expectedMx, ttl:3600, domainName:domain}) : '—'}</td></tr>
            <tr><td>SPF</td><td style="font-family:monospace;font-size:12px;">${esc(spfRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(spfPublished)}</td><td>${window.statusBadge(spfStatus.status, spfStatus.cls)}</td><td>${window.copyRowButtons({host:'@', type:'SPF', value:spfRecommended, ttl:3600, domainName:domain})}</td></tr>
            <tr><td>DKIM</td><td style="font-family:monospace;font-size:12px;">${dkimKey ? window.fmtVal(dkimKey, 60) : '—'}</td><td style="font-family:monospace;font-size:12px;">${dkimPublished ? window.fmtVal(dkimPublished, 60) : '—'}</td><td>${window.statusBadge(dkimStatus.status, dkimStatus.cls)}</td><td>${dkimKey ? window.copyRowButtons({host:dkimHost, type:'TXT', value:dkimKey, ttl:3600, domainName:domain}) : '—'}</td></tr>
            <tr><td>DMARC</td><td style="font-family:monospace;font-size:12px;">${esc(dmarcRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(dmarcPublished)}</td><td>${window.statusBadge(dmarcStatus.status, dmarcStatus.cls)}</td><td>${window.copyRowButtons({host:'_dmarc.' + domain, type:'TXT', value:dmarcRecommended, ttl:3600, domainName:domain})}</td></tr>
          </tbody>
        </table>
      </div>

      <h3 style="font-size:13px;margin-bottom:8px;">Recommended Records</h3>
      <div class="table-wrap" style="margin-bottom:12px;">
        <table>
          <thead><tr><th>Type</th><th>Recommended</th><th>Published</th><th>Status</th><th>Actions</th></tr></thead>
          <tbody>
            <tr><td>Autodiscover</td><td style="font-family:monospace;font-size:12px;">${esc(autoRecommended ? 'CNAME → ' + autoRecommended : 'N/A')}</td><td style="font-family:monospace;font-size:12px;">${esc(autoPublished) || '—'}</td><td>${window.statusBadge(autoStatus.status, autoStatus.cls)}</td><td>${autoRecommended ? window.copyRowButtons({host:autoHost, type:'CNAME', value:autoRecommended + '.', ttl:3600, domainName:domain}) : '—'}</td></tr>
            <tr><td>MTA-STS</td><td style="font-family:monospace;font-size:12px;">${esc(mtaRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(mtaPublished)}</td><td>${window.statusBadge(mtaStatus.status, mtaStatus.cls)}</td><td>${window.copyRowButtons({host:'_mta-sts.' + domain, type:'TXT', value:mtaRecommended, ttl:3600, domainName:domain})}<br><span style="font-size:10px;color:var(--text3);">Also requires HTTPS policy at https://mta-sts.${esc(domain)}/.well-known/mta-sts.txt</span></td></tr>
            <tr><td>TLS-RPT</td><td style="font-family:monospace;font-size:12px;">${esc(tlsRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(tlsPublished)}</td><td>${window.statusBadge(tlsStatus.status, tlsStatus.cls)}</td><td>${window.copyRowButtons({host:'_smtp._tls.' + domain, type:'TXT', value:tlsRecommended, ttl:3600, domainName:domain})}<br><span style="font-size:10px;color:var(--text3);">Requires mailbox tlsrpt@${esc(domain)}</span></td></tr>
            <tr><td>CAA</td><td style="font-family:monospace;font-size:12px;">${esc(caaRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(caaPublished)}</td><td>${window.statusBadge(caaStatus.status, caaStatus.cls)}</td><td>${window.copyRowButtons({host:'@', type:'CAA', value:caaRecommended, ttl:3600, domainName:domain})}</td></tr>
          </tbody>
        </table>
      </div>

      <div style="text-align:right;font-size:11px;color:var(--text3);margin-bottom:8px;">Last checked: ${ts}</div>

      ${phpMailHtml}
    </div>`;

  window.attachDataCopyListener('mail-tab-content');
}

async function refreshMailTab() {
  const dd = window._domainDetailData;
  if (!dd) return;
  const domain = dd.domainRow.domain;
  const md = dd.mailDomain;
  DnsCache.clear(domain);
  if (md) {
    DnsCache.clear('_dmarc.' + domain);
    DnsCache.clear('_mta-sts.' + domain);
    DnsCache.clear('_smtp._tls.' + domain);
    DnsCache.clear('autodiscover.' + domain);
    if (md.dkim_public_key_dns) {
      const sel = md.dkim_selector || 'dkim';
      DnsCache.clear(sel + '._domainkey.' + domain);
    }
  }
  loadDomainMail();
}
// ===== SECURITY TAB =====
let _dmarcSelection = '';

// Track open evidence panels (accordion: only one at a time)
// ===== SECURITY TAB =====
let _openEvidencePanel = null;

// Shared accordion helpers
function closeEvidencePanel() {
  if (_openEvidencePanel) {
    const panel = document.getElementById(_openEvidencePanel);
    if (panel) panel.remove();
    _openEvidencePanel = null;
  }
}

function toggleEvidencePanel(panelId, anchorEl, html) {
  // Same panel clicked → close it
  if (_openEvidencePanel === panelId) {
    closeEvidencePanel();
    return;
  }
  // Different panel open → close first
  closeEvidencePanel();
  // Insert new panel after anchor
  const container = document.createElement('div');
  container.id = panelId;
  container.innerHTML = html;
  anchorEl.after(container);
  _openEvidencePanel = panelId;
  // Wire Dismiss button
  const dismissBtn = container.querySelector('[data-evidence-dismiss]');
  if (dismissBtn && activeDomainsLifecycle && activeDomainsLifecycle.addEventListener) activeDomainsLifecycle.addEventListener(dismissBtn, 'click', closeEvidencePanel);
  else if (dismissBtn) dismissBtn.addEventListener('click', closeEvidencePanel);
}

function evidenceHtml(type, configured, published, dnsDetails, copyValue, steps) {
  const info = getEvidenceReason(type, configured, published, dnsDetails || '');
  const stepsHtml = steps && steps.length
    ? '<ol>' + steps.map(s => '<li>' + esc(s) + '</li>').join('') + '</ol>'
    : '<p>' + esc(info.fix) + '</p>';
  return '<div class="evidence-panel" style="background:var(--bg3);border:1px solid var(--border);border-radius:6px;padding:12px;margin:8px 0;">'
    + '<div style="margin-bottom:8px;"><strong style="font-size:13px;">' + esc(type) + '</strong></div>'
    + '<div style="display:grid;gap:6px;font-size:12px;">'
    + '<div><strong>Expected (ContainerCP):</strong><br><code style="word-break:break-all;">' + esc(configured) + '</code></div>'
    + '<div><strong>Published (public DNS):</strong><br><code style="word-break:break-all;">' + esc(published) + '</code></div>'
    + '<div><strong>Reason:</strong> ' + esc(info.reason) + '</div>'
    + '<div><strong>How to fix:</strong>' + stepsHtml + '</div>'
    + (dnsDetails ? '<div><strong>DNS Response Details:</strong><br><code style="font-size:11px;word-break:break-all;">' + esc(dnsDetails) + '</code></div>' : '')
    + '</div>'
    + '<div style="margin-top:8px;display:flex;gap:6px;">'
    + '<button class="btn btn-sm btn-primary" data-copy="' + copyValue.replace(/"/g, '&quot;').replace(/'/g, '&#39;') + '">Copy Correct Record</button>'
    + '<button class="btn btn-sm" data-evidence-dismiss="1">Dismiss</button>'
    + '</div></div>';
}

function getEvidenceReason(type, dnsDetails) {
  const reasons = {
    'DMARC_POLICY_MISMATCH': { reason: 'The DMARC policy (p=) field differs between the recommended and published values.' },
    'MTA_STS_NOT_FOUND': { reason: 'No MTA-STS TXT record found. Mail delivery without TLS may be insecure.' },
    'CAA_MISSING': { reason: 'No CAA record found. Any CA can issue certificates for your domain.' },
    'TLS_RPT_NOT_FOUND': { reason: 'No TLS-RPT record found. Delivery failure reports will not be sent.' },
  };
  return reasons[type] || { reason: 'Unexpected DNS configuration. Review the expected and published values.' };
}

function getEvidenceSteps(type, domain) {
  const shared = ['Copy the correct record using the button below.', 'Log in to your DNS provider\'s control panel.', 'Navigate to the DNS zone for ' + domain + '.'];
  const specifics = {
    'DMARC_POLICY_MISMATCH': ['Update the TXT record at _dmarc.' + domain + ' with the recommended value.', 'Wait for DNS propagation (up to 48 hours).', 'Click Check Again to verify.'],
    'MTA_STS_NOT_FOUND': ['Add a new TXT record with Host: _mta-sts and the value below.', 'Optionally create the HTTPS policy file at https://mta-sts.' + domain + '/.well-known/mta-sts.txt', 'Click Check Again to verify.'],
    'CAA_MISSING': ['Add a new CAA record with the value: 0 issue "letsencrypt.org"', 'Click Check Again to verify.'],
    'TLS_RPT_NOT_FOUND': ['Add a new TXT record with Host: _smtp._tls and the value below.', 'Ensure tlsreports@' + domain + ' exists as a mailbox or alias.', 'Click Check Again to verify.'],
  };
  const extra = specifics[type] || ['Add or update the DNS record with the correct value.', 'Click Check Again to verify.'];
  return [...shared, ...extra];
}

// Recommendation definitions (single source of truth)
function getRecDefs(domain, serverHostname, dmarcCurrent, dmarcPublished, stsPublished, caaPublished, tlsPublished, autoPublished) {
  function p(v) { return v || '(not published)'; }
  const all = {
    'dmarc': {
      type: 'DMARC_POLICY_MISMATCH',
      configured: dmarcCurrent || '',
      published: p(dmarcPublished),
      copyValue: dmarcCurrent || '',
    },
    'mta-sts': {
      type: 'MTA_STS_NOT_FOUND',
      configured: 'v=STSv1; id=1',
      published: p(stsPublished),
      copyValue: 'v=STSv1; id=1',
    },
    'caa': {
      type: 'CAA_MISSING',
      configured: '0 issue "letsencrypt.org"',
      published: p(caaPublished),
      copyValue: '0 issue "letsencrypt.org"',
    },
    'tls-rpt': {
      type: 'TLS_RPT_NOT_FOUND',
      configured: 'v=TLSRPTv1; rua=mailto:tlsreports@' + domain,
      published: p(tlsPublished),
      copyValue: 'v=TLSRPTv1; rua=mailto:tlsreports@' + domain,
    },
    'autodiscover': {
      type: 'AUTODISCOVER_NOT_FOUND',
      configured: serverHostname || 'N/A',
      published: p(autoPublished),
      copyValue: 'autodiscover.' + domain + '. 3600 IN CNAME ' + (serverHostname || 'N/A') + '.',
    },
  };
  return all;
}

function loadDomainSecurity() {
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  const dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }
  const {domainRow, mailDomain, serverHostname} = dd;
  const domain = domainRow.domain;
  closeEvidencePanel();

  (async () => {
    // Fetch live DNS for DMARC + all recommendations
    // Each fetch has its own try/catch so one failure doesn't block the tab
    let dmarcPublished = '', stsPublished = '', caaPublished = '', tlsPublished = '', autoPublished = '';
    try {
      const dmarcDns = await fetchDnsForFqdn('_dmarc.' + domain, 'TXT');
      if (dmarcDns) { const r = window.getDnsRecs(dmarcDns, 'TXT'); if (r.length) dmarcPublished = r[0].value; }
    } catch(e) { console.error('Failed to fetch _dmarc.' + domain, e); }
    try {
      const d = await fetchDnsForFqdn('_mta-sts.' + domain, 'TXT');
      if (d) { const r = window.getDnsRecs(d, 'TXT'); if (r.length) stsPublished = r[0].value; }
    } catch(e) { console.error('Failed to fetch _mta-sts.' + domain, e); }
    try {
      const d = await fetchDnsForFqdn(domain, 'CAA');
      if (d) { const r = window.getDnsRecs(d, 'CAA'); if (r.length) caaPublished = r.map(x => x.value).join(', '); }
    } catch(e) { console.error('Failed to fetch CAA for ' + domain, e); }
    try {
      const d = await fetchDnsForFqdn('_smtp._tls.' + domain, 'TXT');
      if (d) { const r = window.getDnsRecs(d, 'TXT'); if (r.length) tlsPublished = r[0].value; }
    } catch(e) { console.error('Failed to fetch _smtp._tls.' + domain, e); }
    try {
      const d = await fetchDnsForFqdn('autodiscover.' + domain, 'CNAME,A');
      if (d) {
        const c = window.getDnsRecs(d, 'CNAME');
        if (c.length) autoPublished = 'CNAME ' + c[0].value;
        else { const a = window.getDnsRecs(d, 'A'); if (a.length) autoPublished = 'A ' + a[0].value; }
      }
    } catch(e) { console.error('Failed to fetch autodiscover.' + domain, e); }

    const dmarcCurrent = _dmarcSelection || 'v=DMARC1; p=none;';
    const recDefs = getRecDefs(domain, serverHostname, dmarcCurrent, dmarcPublished, stsPublished, caaPublished, tlsPublished, autoPublished);
    const dmarcHost = '_dmarc.' + domain;
    const dmarcFqdn = dmarcHost + '.';
    const dmarcFull = dmarcFqdn + ' 3600 IN TXT "' + dmarcCurrent + '"';
    const hasRua = dmarcCurrent.includes('rua=');
    const copyWithRua = hasRua ? dmarcCurrent : dmarcCurrent.replace(/;?\s*$/, '; rua=mailto:dmarc@' + domain);

    // Build entire HTML in one string — single innerHTML assignment
    var html = '';

    // Header
    html += '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;">'
      + '<h3 style="font-size:14px;">DMARC Policy</h3>'
      + '<button class="btn btn-sm" data-security-check-again="1">Check Again</button></div>'
      + '<div style="margin-bottom:12px;font-size:12px;color:var(--text3);">'
      + (dmarcPublished ? 'Current in DNS: <code>' + esc(dmarcPublished) + '</code>' : 'No DMARC record found in DNS') + '</div>';

    // DMARC policy cards
    html += '<div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px;margin-bottom:16px;">'
      + '<div class="card" style="cursor:pointer;' + (_dmarcSelection && _dmarcSelection.includes('p=none') ? 'border-color:var(--primary);' : '') + '" data-dmarc-policy="v=DMARC1; p=none;">'
      + '<h3>Monitor</h3><div style="margin-top:8px;font-size:12px;font-family:monospace;">v=DMARC1; p=none;</div>'
      + '<div style="margin-top:4px;font-size:11px;color:var(--text3);">No action taken on failing messages</div></div>'
      + '<div class="card" style="cursor:pointer;' + (_dmarcSelection && _dmarcSelection.includes('p=quarantine') ? 'border-color:var(--primary);' : '') + '" data-dmarc-policy="v=DMARC1; p=quarantine; rua=mailto:dmarc@' + domain + '">'
      + '<h3>Quarantine</h3><div style="margin-top:8px;font-size:12px;font-family:monospace;">v=DMARC1; p=quarantine;</div>'
      + '<div style="margin-top:4px;font-size:11px;color:var(--text3);">Tag suspicious emails as spam</div></div>'
      + '<div class="card" style="cursor:pointer;' + (_dmarcSelection && _dmarcSelection.includes('p=reject') ? 'border-color:var(--primary);' : '') + '" data-dmarc-policy="v=DMARC1; p=reject; rua=mailto:dmarc@' + domain + '">'
      + '<h3>Reject</h3><div style="margin-top:8px;font-size:12px;font-family:monospace;">v=DMARC1; p=reject;</div>'
      + '<div style="margin-top:4px;font-size:11px;color:var(--text3);">Block failing emails entirely</div></div></div>';

    // DMARC Preview + Comparison
    if (_dmarcSelection) {
      var cmp = '';
      if (dmarcPublished) {
        var r = window.compareDmarcRecords(_dmarcSelection, dmarcPublished);
        var dmarcDef = recDefs['dmarc'];
        cmp = '<div class="card" style="margin-top:8px;" data-security-record="dmarc"'
          + ' data-evidence-configured="' + escAttr(dmarcDef ? dmarcDef.configured : '') + '"'
          + ' data-evidence-published="' + escAttr(dmarcDef ? dmarcDef.published : '') + '"'
          + ' data-evidence-copy="' + escAttr(dmarcDef ? dmarcDef.copyValue : '') + '">'
          + '<div style="font-size:12px;">Comparison</div>'
          + '<div style="margin-top:6px;display:grid;grid-template-columns:1fr 1fr;gap:8px;font-size:12px;">'
          + '<div><strong>Recommended:</strong><br><code style="font-size:11px;word-break:break-all;">' + esc(_dmarcSelection) + '</code></div>'
          + '<div><strong>Published:</strong><br><code style="font-size:11px;word-break:break-all;">' + esc(dmarcPublished) + '</code></div></div>'
          + '<div style="margin-top:6px;">' + window.statusBadge(r.status, r.cls)
          + (r.status === 'Mismatch' ? '<button class="btn btn-sm" data-security-why="1" data-evidence-type="DMARC_POLICY_MISMATCH" data-security-record-key="dmarc">Why?</button>' : '') + '</div></div>';
      }
      html += '<div class="card"><h3>Your DMARC Record Preview</h3>'
        + '<div style="margin-top:8px;font-size:12px;font-family:monospace;word-break:break-all;background:var(--bg3);padding:8px;border-radius:4px;">' + esc(dmarcFull) + '</div>'
        + '<div style="margin-top:8px;display:flex;gap:6px;flex-wrap:wrap;">'
        + '<button class="btn btn-sm btn-primary" data-copy="' + escAttr(_dmarcSelection) + '">Copy Record</button>'
        + '<button class="btn btn-sm" data-copy="' + escAttr(copyWithRua) + '">Copy with RUA</button>'
        + '<button class="btn btn-sm" data-copy="' + escAttr(dmarcFull) + '">Copy Full Record</button></div>'
        + '<div style="margin-top:8px;font-size:11px;color:var(--yellow);">⚠️ Start with p=none to monitor, then escalate to quarantine after 1-2 weeks.</div>'
        + cmp + '</div>';
    }

    // Additional recommendations
    function secCard(key, title, host, type, val, btns) {
      var def = recDefs[key];
      if (!def) return '';
      return '<div class="card" data-security-record="' + key + '"'
        + ' data-evidence-configured="' + escAttr(def.configured) + '"'
        + ' data-evidence-published="' + escAttr(def.published) + '"'
        + ' data-evidence-copy="' + escAttr(def.copyValue) + '">'
        + '<h3>' + title + '</h3>'
        + '<div style="margin-top:8px;font-size:12px;"><div>' + esc(val) + '</div>'
        + '<div style="margin-top:6px;font-family:monospace;font-size:11px;">Host: ' + esc(host) + '<br>Type: ' + esc(type) + '<br>Value: ' + esc(def.configured) + '</div></div>'
        + '<div style="margin-top:8px;display:flex;gap:6px;flex-wrap:wrap;">' + btns + '</div></div>';
    }
    function recWhy(key) {
      var def = recDefs[key];
      if (!def) return '';
      return '<button class="btn btn-sm" data-security-why="1" data-evidence-type="' + escAttr(def.type) + '" data-security-record-key="' + escAttr(key) + '">Why?</button>';
    }
    function copyBtnHtml(v) { return '<button class="btn btn-sm" data-copy="' + escAttr(v) + '">Copy Record</button>'; }

    html += '<h3 style="font-size:14px;margin:16px 0 8px;">Additional Recommendations</h3>'
      + '<div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:12px;">'
      + secCard('mta-sts', 'MTA-STS', '_mta-sts', 'TXT', 'Ensures TLS is used for mail delivery (RFC 8461).', copyBtnHtml('v=STSv1; id=1') + recWhy('mta-sts'))
      + secCard('caa', 'CAA', '@', 'CAA', 'Certification Authority Authorization lets you specify which CAs can issue certificates.', copyBtnHtml('0 issue "letsencrypt.org"') + recWhy('caa'))
      + secCard('tls-rpt', 'TLS-RPT', '_smtp._tls', 'TXT', 'TLS-RPT sends delivery failure reports to your email.', copyBtnHtml(recDefs['tls-rpt'].configured) + recWhy('tls-rpt'))
      + secCard('autodiscover', 'Autodiscover', 'autodiscover', 'CNAME', 'Autodiscover configures email clients automatically.', copyBtnHtml(recDefs['autodiscover'].copyValue) + (serverHostname ? recWhy('autodiscover') : ''))
      + '</div>';

    // Single innerHTML assignment — all elements inside #security-tab-content
    content.innerHTML = '<div id="security-tab-content">' + html + '</div>';

    // Unified event delegation
    attachSecurityDelegation();
  })().catch(function(err) {
    console.error('Security tab load failed', err);
    var el = document.getElementById('domain-tab-content');
    if (el) el.innerHTML = '<div class="empty-state">Failed to load Security tab</div>';
  });
}

// Single event delegation for Security tab
function attachSecurityDelegation() {
  var sec = document.getElementById('security-tab-content');
  if (!sec) return;
  const listener = function(e) {
    // Copy buttons
    var copyBtn = e.target.closest('[data-copy]');
    if (copyBtn) {
      copyText(copyBtn.getAttribute('data-copy'), 'Copied');
      return;
    }
    // Dismiss evidence
    if (e.target.closest('[data-evidence-dismiss]')) {
      closeEvidencePanel();
      return;
    }
    // DMARC policy selection
    var policyCard = e.target.closest('[data-dmarc-policy]');
    if (policyCard) {
      _dmarcSelection = policyCard.getAttribute('data-dmarc-policy');
      closeEvidencePanel();
      loadDomainSecurity();
      return;
    }
    // Check Again
    if (e.target.closest('[data-security-check-again]')) {
      closeEvidencePanel();
      var dd = window._domainDetailData;
      if (dd) {
        var d = dd.domainRow.domain;
        DnsCache.clear(d);
        DnsCache.clear('_dmarc.' + d);
        DnsCache.clear('_mta-sts.' + d);
        DnsCache.clear('_smtp._tls.' + d);
        DnsCache.clear('autodiscover.' + d);
      }
      loadDomainSecurity();
      return;
    }
    // Why? evidence
    var whyBtn = e.target.closest('[data-security-why]');
    if (!whyBtn) return;
    var key = whyBtn.getAttribute('data-security-record-key');
    var type = whyBtn.getAttribute('data-evidence-type');
    var dd = window._domainDetailData;
    if (!dd) return;
    var card = sec.querySelector('[data-security-record="' + key + '"]');
    if (!card) return;
    var configured = card.getAttribute('data-evidence-configured') || '';
    var published = card.getAttribute('data-evidence-published') || '(not published)';
    var copyValue = card.getAttribute('data-evidence-copy') || '';
    var steps = getEvidenceSteps(type, dd.domainRow.domain);
    var html = evidenceHtml(type, configured, published, '', copyValue, steps);
    toggleEvidencePanel('ev-' + key, card, html);
  };
  if (activeDomainsLifecycle && activeDomainsLifecycle.addEventListener) activeDomainsLifecycle.addEventListener(sec, 'click', listener);
  else sec.addEventListener('click', listener);
}

window.selectDmarcPolicy = function(value) {
  _dmarcSelection = value;
  closeEvidencePanel();
  loadDomainSecurity();
};
function loadDomainHealth() {
  var content = document.getElementById('domain-tab-content');
  if (!content) return;
  var dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }

  content.innerHTML = '<div class="empty-state">Computing health score...</div>';

  var domain = dd.domainRow.domain;

  void(dd);
  var domain = dd.domainRow.domain;
  window.HealthCache.load(domain, dd.domainRow, dd.mailDomain, dd.serverHostname, {force: true}).then(function(result) {

    if (!result || result.score == null) {
      content.innerHTML = '<div class="empty-state">No checks applicable for this domain.</div>';
      return;
    }

    var ts = result.computed_at ? new Date(result.computed_at).toLocaleTimeString() : new Date().toLocaleTimeString();

    if (result.score == null) {
      content.innerHTML = '<div class="empty-state">No checks applicable for this domain.</div>';
      return;
    }

    // Build breakdown rows with Configured + Published columns
    var rows = '';
    for (var i = 0; i < result.breakdown.length; i++) {
      var c = result.breakdown[i];
      var clsLabel = c.cls === 'req' ? 'Required' : c.cls === 'rec' ? 'Recommended' : 'Informational';
      var clsBadge = c.cls === 'req' ? 'badge-err' : c.cls === 'rec' ? 'badge-warn' : 'badge-info';
      var earned = c.earned !== null && c.earned !== undefined ? c.earned : '—';
      var scoreStr = c.weight > 0 ? earned + '/' + c.weight : '—';
      rows += '<tr>'
        + '<td><span class="badge ' + clsBadge + '">' + clsLabel + '</span></td>'
        + '<td>' + esc(c.label) + '</td>'
        + '<td>' + window.statusBadge(c.status || 'N/A', c.status === 'Match' || c.status === 'Active' || c.status === 'Running' ? 'badge-ok' : c.status === 'N/A' ? 'badge-info' : c.status === 'Unexpected' || c.status === 'Expiring' || c.status === 'Starting' ? 'badge-warn' : 'badge-err') + '</td>'
        + '<td>' + c.weight + '</td>'
        + '<td style="font-family:monospace;font-size:12px;">' + esc(typeof c.configured === 'string' ? c.configured.substring(0, 30) : '') + '</td>'
        + '<td style="font-family:monospace;font-size:12px;">' + esc(typeof c.published === 'string' ? c.published.substring(0, 30) : '') + '</td>'
        + '<td>' + earned + '</td>'
        + '<td>' + scoreStr + '</td>'
        + '</tr>';
    }

    var gradeColor = result.grade === 'Excellent' ? 'badge-ok' : result.grade === 'Good' ? 'badge-info' : result.grade === 'Fair' ? 'badge-warn' : 'badge-err';

    content.innerHTML = '<div id="health-tab-content">'
      + '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;">'
      + '<h3 style="font-size:14px;">Health Score</h3>'
      + '<button class="btn btn-sm" data-health-check-again="1">Check Again</button></div>'

      + '<div class="card" style="text-align:center;margin-bottom:16px;">'
      + '<div style="font-size:48px;font-weight:700;">' + result.score + '</div>'
      + '<div style="font-size:14px;">/ 100</div>'
      + '<div style="margin-top:8px;"><span class="badge ' + gradeColor + '" style="font-size:14px;padding:4px 16px;">' + esc(result.grade) + '</span></div>'
      + '<div style="margin-top:8px;font-size:12px;color:var(--text3);">' + result.earnedWeight + ' of ' + result.applicableWeight + ' weighted points</div>'
      + '<div style="margin-top:4px;font-size:11px;color:var(--text3);">Last checked: ' + ts + '</div></div>'

      + '<h3 style="font-size:13px;margin-bottom:8px;">Check Details</h3>'
      + '<div class="table-wrap">'
      + '<table><thead><tr><th>Class</th><th>Check</th><th>Status</th><th>Wt</th><th>Configured</th><th>Published</th><th>Got</th><th>Score</th></tr></thead>'
      + '<tbody>' + rows + '</tbody></table></div></div>';

    attachHealthDelegation();
  }).catch(function(err) {
    console.error('Health tab failed', err);
    content.innerHTML = '<div class="empty-state">Failed to load Health tab</div>';
  });
}

function attachHealthDelegation() {
  var root = document.getElementById('health-tab-content');
  if (!root) return;
  const listener = function(e) {
    if (e.target.closest('[data-health-check-again]')) {
      var dd = window._domainDetailData;
      if (dd) {
        var d = dd.domainRow.domain;
        // Clear DNS cache for all health-related FQDNs
        DnsCache.clear(d);
        DnsCache.clear('_dmarc.' + d);
        DnsCache.clear('_mta-sts.' + d);
        DnsCache.clear('_smtp._tls.' + d);
        DnsCache.clear('autodiscover.' + d);
        if (dd.mailDomain && dd.mailDomain.dkim_public_key_dns) {
          var sel = dd.mailDomain.dkim_selector || 'dkim';
          DnsCache.clear(sel + '._domainkey.' + d);
        }
        // Invalidate HealthCache so next load is fresh
        window.HealthCache.invalidate(d);
      }
      loadDomainHealth();
    }
  };
  if (activeDomainsLifecycle && activeDomainsLifecycle.addEventListener) activeDomainsLifecycle.addEventListener(root, 'click', listener);
  else root.addEventListener('click', listener);
}
async function removeDomain(domain) {
  if (!confirm('Remove domain '+domain+'?')) return;
  try { const res = await apiPost('/api/domains/remove',{domain}); if(res.success){toast('Domain removed','success');loadDomains($('page'), null, activeDomainsLifecycle);}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

const domainsPage = { mount: loadDomains, unmount() { activeDomainsLifecycle = null; } };
const domainDetailPage = { mount: loadDomainDetail, unmount() { activeDomainsLifecycle = null; } };
export { loadDomains, loadDomainDetail, domainsPage, domainDetailPage };
Object.assign(window, { domainTypeBadge, domainSslBadge, domainUsableHttps, loadDomains, loadDomainDetail, switchDomainTab, fetchDnsForFqdn, runSystemAction, loadSslDetails, loadDomainOverview, refreshDomainOverview, loadDomainDnsRecords, refreshDnsRecordsTab, loadDomainMail, refreshMailTab, closeEvidencePanel, toggleEvidencePanel, evidenceHtml, getEvidenceReason, getEvidenceSteps, getRecDefs, loadDomainSecurity, attachSecurityDelegation, loadDomainHealth, attachHealthDelegation, removeDomain });
