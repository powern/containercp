import { esc } from '../core/utils.js';

export function card(label, count, color, sub) {
  return '<div class="card"><div style="font-size:13px;color:var(--text2);">' + esc(label) + '</div><div class="count ' + (color || '') + '">' + esc(String(count)) + '</div>' + (sub ? '<div style="font-size:12px;color:var(--text3);margin-top:6px;">' + esc(sub) + '</div>' : '') + '</div>';
}

export function pageHeader(title, subtitle, actionsHtml, eyebrow) {
  return '<div class="ui-page-header"><div>'
    + (eyebrow ? '<div class="ui-page-eyebrow">' + esc(eyebrow) + '</div>' : '')
    + '<h1 class="ui-page-title">' + esc(title) + '</h1>'
    + (subtitle ? '<p class="ui-page-subtitle">' + esc(subtitle) + '</p>' : '')
    + '</div>'
    + (actionsHtml ? '<div class="page-actions">' + actionsHtml + '</div>' : '')
    + '</div>';
}

export function summaryCard(label, value, tone, help) {
  return '<div class="ui-summary-card ' + esc(tone || 'neutral') + '"><div class="ui-summary-label">' + esc(label) + '</div><div class="ui-summary-value">' + esc(String(value)) + '</div>' + (help ? '<div class="ui-summary-help">' + esc(help) + '</div>' : '') + '</div>';
}

export function summaryCards(items) {
  return '<div class="ui-summary-grid">' + (items || []).map(item => summaryCard(item.label, item.value, item.tone || item.className, item.help)).join('') + '</div>';
}
