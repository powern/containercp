import {
  pageHeader, summaryCards
} from '../core/context.js';


/* ===== WEBMAIL ===== */
async function loadWebmail(p) {
  p.innerHTML = `${pageHeader('Webmail', 'Mailbox user access through the installed webmail client.', '', 'Mail')}
    ${summaryCards([{label:'Client', value:'SnappyMail', tone:'healthy', help:'External mailbox login'}])}
    <div class="card">
      <div class="card-header"><h3>SnappyMail Webmail</h3></div>
      <div style="padding:16px;text-align:center;">
        <p style="margin-bottom:16px;color:var(--text2);">Access your mail via SnappyMail webmail client.</p>
        <a href="/webmail/" target="_blank" class="btn btn-primary">Open Webmail</a>
        <p style="margin-top:12px;font-size:12px;color:var(--text2);">Login with your full email address and mailbox password.</p>
      </div>
    </div>`;
}

const webmailPage = { mount: loadWebmail };
export { loadWebmail, webmailPage };
