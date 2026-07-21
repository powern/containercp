export function card(label, count, color, sub) {
  return '<div class="card"><div style="font-size:13px;color:var(--text2);">'+label+'</div><div class="count '+(color||'')+'">'+count+'</div>'+(sub?'<div style="font-size:12px;color:var(--text3);margin-top:6px;">'+sub+'</div>':'')+'</div>';
}
