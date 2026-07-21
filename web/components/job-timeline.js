export { renderRotationJobTimeline, renderWordPressRotationDiagnostics } from '../core/jobs.js';

import { esc } from '../core/utils.js';

export function jobTimeline(job) {
  const steps = (job && job.steps) || [];
  if (!steps.length) return '<div class="ui-state ui-state-loading">No job timeline is available yet.</div>';
  return '<div class="db-job-box">' + steps.map(step => {
    const state = step.failed ? 'failed' : (step.completed ? 'completed' : (step.started ? 'started' : 'pending'));
    const cls = step.failed ? 'badge-err' : (step.completed ? 'badge-ok' : (step.started ? 'badge-warn' : 'badge-info'));
    return '<div class="db-job-step"><div><strong>' + esc(step.name || step.id || 'step') + '</strong></div><div><span class="badge ' + cls + '">' + esc(state) + '</span></div><div>' + esc(step.message || step.result || '') + '</div></div>';
  }).join('') + '</div>';
}
