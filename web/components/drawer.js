import { esc, escAttr } from '../core/utils.js';

export function closeDrawerById(drawerId, backdropId) { const drawer = document.getElementById(drawerId); const backdrop = document.getElementById(backdropId); if (drawer) drawer.classList.remove('open'); if (backdrop) backdrop.style.display='none'; }

export function drawerShell(title, subtitle, bodyHtml, closeAction) {
  return '<div class="ui-drawer-header"><div><h2>' + esc(title) + '</h2>' + (subtitle ? '<p>' + esc(subtitle) + '</p>' : '') + '</div><button class="btn-icon" onclick="' + escAttr(closeAction || '') + '" aria-label="Close detail">&times;</button></div><div class="ui-detail-content">' + (bodyHtml || '') + '</div>';
}

export function drawerSection(title, contentHtml) {
  return '<section class="ui-detail-section"><h3>' + esc(title) + '</h3>' + (contentHtml || '') + '</section>';
}

export function statusRow(label, valueHtml) {
  return '<div style="display:flex;justify-content:space-between;gap:12px;padding:6px 0;border-bottom:1px solid var(--border);"><span style="color:var(--text2);">' + esc(label) + '</span><span>' + (valueHtml || '') + '</span></div>';
}
