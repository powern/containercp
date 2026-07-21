export function confirmAction(message) { return window.confirm(message); }

export function destructiveConfirm(message, resourceName) {
  return window.confirm(message + (resourceName ? '\n\nResource: ' + resourceName : ''));
}

export function typedConfirmationBody(resourceName, inputId, actionLabel) {
  return '<p>Type <strong>' + resourceName + '</strong> to confirm.</p>'
    + '<input id="' + inputId + '" autocomplete="off" placeholder="' + resourceName + '" class="ui-input">'
    + '<div style="display:flex;justify-content:flex-end;gap:8px;margin-top:14px;">'
    + '<button class="btn btn-sm" onclick="hideModal()">Cancel</button>'
    + '<button class="btn btn-sm btn-warning" id="' + inputId + '-action">' + (actionLabel || 'Confirm') + '</button>'
    + '</div>';
}
