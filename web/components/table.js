import { esc } from '../core/utils.js';

export function tb(title) {
  return '<div class="table-toolbar"><div style="font-weight:600;font-size:14px;">' + esc(title) + '</div><label class="search-box"><span class="sr-only">Search ' + esc(title) + '</span><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>&nbsp;<input type="text" placeholder="Search..." oninput="window.searchTerm=this.value;window.renderTable&&window.renderTable()"></label></div>';
}

export function buildTable(columns, rows, emptyMsg) {
  if (!rows.length) return '<div class="empty-state">' + esc(emptyMsg || 'No data') + '</div>';
  return '<div class="ui-table-wrap"><table><thead><tr>' + columns.map(c => '<th>' + esc(c.label) + '</th>').join('') + '</tr></thead><tbody>' + rows.map(r => '<tr>' + columns.map(c => '<td>' + (c.html ? c.html(r) : esc(r[c.key] || '')) + '</td>').join('') + '</tr>').join('') + '</tbody></table></div>';
}

export function responsiveInventoryCards(rows, renderCard, emptyMsg) {
  if (!rows.length) return '<div class="empty-state">' + esc(emptyMsg || 'No data') + '</div>';
  return '<div class="ui-mobile-list">' + rows.map(renderCard).join('') + '</div>';
}
