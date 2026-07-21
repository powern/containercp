import { api } from './api.js';
import { $ } from './dom.js';
import { esc } from './utils.js';

export function pollJobProgress(jobId, onComplete, lifecycle) {
  const setPollInterval = lifecycle && lifecycle.setInterval ? lifecycle.setInterval.bind(lifecycle) : setInterval;
  const clearPollInterval = clearInterval;
  const interval = setPollInterval(async () => {
    if (lifecycle && !lifecycle.isActive()) return;
    try {
      const res = await api('/api/jobs?id=' + jobId);
      if (lifecycle && !lifecycle.isActive()) return;
      if (res.success && res.data) {
        const job = res.data;
        const pbar = $('progress-bar');
        const pstep = $('progress-step');
        const pstatus = $('progress-status');
        if (pbar) pbar.style.width = job.progress + '%';
        const currentStep = job.steps && job.steps[job.current_step];
        const currentStepText = typeof currentStep === 'string' ? currentStep : (currentStep && currentStep.name);
        if (pstep) pstep.textContent = job.message || currentStepText || 'Running...';
        if (pstatus) pstatus.textContent = job.status;

        if (job.status === 'completed' || job.status === 'failed') {
          clearPollInterval(interval);
          if (job.status === 'failed') {
            if (pbar) pbar.style.background = 'var(--red)';
            if (pstep) pstep.textContent = 'Error: ' + (job.message || 'Deployment failed');
          } else {
            if (onComplete) onComplete();
          }
        }
      }
    } catch(e) {
      clearPollInterval(interval);
    }
  }, 500);
  return () => clearInterval(interval);
}

export async function pollRotationJob(jobId, opts, attempts) {
  opts = opts || {};
  const lifecycle = opts.lifecycle;
  if (lifecycle && !lifecycle.isActive()) return;
  attempts = attempts || 0;
  const maxAttempts = opts.maxAttempts || 30;
  if (attempts > maxAttempts) return;
  try {
    const res = await api('/api/jobs?id=' + jobId);
    if (lifecycle && !lifecycle.isActive()) return;
    const job = res.data || {};
    const msg = opts.messageEl ? $(opts.messageEl) : null;
    if (msg) {
      if (opts.renderRunning) msg.innerHTML = opts.renderRunning(jobId, job);
      else msg.textContent = 'Job #' + jobId + ': ' + (job.message || job.status || 'pending');
    }
    if (job.status === 'completed' || job.status === 'failed') {
      if (job.status === 'failed') {
        if (msg) msg.innerHTML = (opts.renderFailed || renderWordPressRotationDiagnostics)(jobId, job);
        if (opts.onFailed) opts.onFailed(job);
      } else {
        if (msg) msg.innerHTML = opts.renderCompleted ? opts.renderCompleted(jobId, job) : '<span class="badge badge-ok">Rotation completed</span>';
        if (opts.onCompleted) opts.onCompleted(job);
      }
      return;
    }
  } catch(e) {}
  const schedule = lifecycle && lifecycle.setTimeout ? lifecycle.setTimeout.bind(lifecycle) : setTimeout;
  const timeout = schedule(() => pollRotationJob(jobId, opts, attempts + 1), opts.intervalMs || 2000);
  return () => clearTimeout(timeout);
}

export function renderWordPressRotationDiagnostics(jobId, job) {
  const failure = job.failure || {};
  const failedStep = (job.steps || []).find(s => s.failed) || {};
  const stage = failure.step_name || failedStep.name || 'Unknown';
  const reason = failure.reason || failedStep.message || job.message || 'Credential rotation failed';
  const code = failure.error_code || failedStep.error_code || '';
  const compensation = failure.compensation_started
    ? (failure.compensation_result || 'started')
    : 'not started';
  const manual = failure.manual_recovery_required ? 'Yes' : 'No';
  const timeline = (job.steps || []).map(step => {
    const state = step.failed ? 'failed' : (step.completed ? 'completed' : (step.started ? 'started' : 'skipped'));
    const color = step.failed ? '#ef4444' : (step.completed ? '#22c55e' : (step.started ? '#f59e0b' : 'var(--text3)'));
    const detail = step.error_code || step.result || step.message || '';
    return '<div style="display:grid;grid-template-columns:170px 80px 1fr;gap:6px;align-items:start;">'
      + '<span>' + esc(step.name || step.id || 'step') + '</span>'
      + '<span style="color:' + color + ';">' + esc(state) + '</span>'
      + '<span style="color:var(--text3);">' + esc(detail) + (step.duration_ms ? ' (' + step.duration_ms + ' ms)' : '') + '</span>'
      + '</div>';
  }).join('');
  return '<div style="display:grid;gap:8px;color:var(--text2);">'
    + '<div style="color:#ef4444;font-weight:600;">Rotation failed</div>'
    + '<div><strong>Stage:</strong> ' + esc(stage) + '</div>'
    + '<div><strong>Reason:</strong> ' + esc(reason) + (code ? ' <span style="color:var(--text3);">(' + esc(code) + ')</span>' : '') + '</div>'
    + '<div><strong>Compensation:</strong> ' + esc(compensation) + '</div>'
    + '<div><strong>Manual recovery required:</strong> ' + esc(manual) + '</div>'
    + '<details style="margin-top:4px;"><summary style="cursor:pointer;">Execution timeline for job #' + esc(String(jobId)) + '</summary>'
    + '<div style="display:grid;gap:4px;margin-top:8px;font-size:11px;">' + timeline + '</div></details>'
    + '</div>';
}

export function renderRotationJobTimeline(job) {
  const steps = job.steps || [];
  if (!steps.length) return '<div style="font-size:12px;color:var(--text3);">No job timeline is available yet.</div>';
  return steps.map(step => {
    const state = step.failed ? 'failed' : (step.completed ? 'completed' : (step.started ? 'started' : 'skipped'));
    const cls = step.failed ? 'badge-err' : (step.completed ? 'badge-ok' : (step.started ? 'badge-warn' : 'badge-info'));
    const detail = step.error_code || step.result || step.message || '';
    return '<div class="db-job-step">'
      + '<div><strong>' + esc(step.name || step.id || 'step') + '</strong></div>'
      + '<div><span class="badge ' + cls + '">' + esc(state) + '</span></div>'
      + '<div>' + esc(detail) + (step.duration_ms ? ' <span style="color:var(--text3);">(' + esc(String(step.duration_ms)) + ' ms)</span>' : '') + '</div>'
      + '</div>';
  }).join('');
}
