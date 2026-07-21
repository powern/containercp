import { summaryCards } from './cards.js';
export function statusSummary(items) { return summaryCards((items || []).map(item => ({ label:item.label, value:item.value, tone:item.tone || item.className || 'neutral', help:item.help || '' }))); }
