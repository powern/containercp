import { esc, escAttr } from '../core/utils.js';

export function noopFilterBar(html) { return html || ''; }

export function searchBox(id, value, placeholder, oninput) {
  return '<label class="ui-search-box"><span class="sr-only">Search</span><input id="' + escAttr(id) + '" value="' + escAttr(value || '') + '" placeholder="' + escAttr(placeholder || 'Search...') + '" oninput="' + escAttr(oninput || '') + '"></label>';
}

export function filterBar(contentHtml) {
  return '<div class="ui-filter-bar">' + (contentHtml || '') + '</div>';
}

export function selectFilter(id, label, options, value, onchange) {
  return '<label class="db-filter"><span>' + esc(label) + '</span><select id="' + escAttr(id) + '" onchange="' + escAttr(onchange || '') + '">'
    + (options || []).map(opt => '<option value="' + escAttr(opt.value) + '"' + (opt.value === value ? ' selected' : '') + '>' + esc(opt.label) + '</option>').join('')
    + '</select></label>';
}
