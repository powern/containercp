#include "core/StartupManager.h"

#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// Bootstrap HTTP server — minimal, no external dependencies.
// Serves the Setup Wizard on 0.0.0.0:80.
// Only required endpoints: /, /api/bootstrap/status, /api/bootstrap/save-hostname,
// /api/bootstrap/check-dns, /api/bootstrap/issue-ssl, /api/bootstrap/complete.

static const char* SETUP_HTML = R"HTML(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ContainerCP — Setup</title>
<style>
* { margin:0; padding:0; box-sizing:border-box; }
body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif; background:#0f0f13; color:#e4e4e7; display:flex; align-items:center; justify-content:center; min-height:100vh; }
.wizard { background:#18181b; border:1px solid #27272a; border-radius:12px; padding:32px; max-width:500px; width:90%; }
.wizard h1 { font-size:22px; margin-bottom:4px; }
.wizard p.sub { color:#a1a1aa; font-size:13px; margin-bottom:24px; }
.field { margin-bottom:16px; }
.field label { display:block; font-size:12px; color:#a1a1aa; margin-bottom:4px; }
.field input { width:100%; padding:10px 12px; border:1px solid #27272a; border-radius:6px; background:#1f1f23; color:#e4e4e7; font-size:14px; outline:none; }
.field input:focus { border-color:#6366f1; }
.btn { padding:10px 20px; border:none; border-radius:6px; font-size:14px; cursor:pointer; }
.btn-primary { background:#6366f1; color:#fff; }
.btn-primary:hover { background:#5457e2; }
.btn:disabled { opacity:0.5; cursor:not-allowed; }
.status { margin-top:12px; padding:8px 12px; border-radius:6px; font-size:13px; display:none; }
.status.ok { display:block; background:#14532d; color:#4ade80; }
.status.err { display:block; background:#450a0a; color:#f87171; }
.status.info { display:block; background:#1e1b4b; color:#818cf8; }
.hidden { display:none; }
.step { margin-bottom:20px; padding-bottom:20px; border-bottom:1px solid #27272a; }
.step:last-child { border-bottom:none; margin-bottom:0; padding-bottom:0; }
.step-num { display:inline-flex; align-items:center; justify-content:center; width:24px; height:24px; border-radius:50%; background:#6366f1; color:#fff; font-size:12px; margin-right:8px; }
.step-title { font-size:15px; font-weight:600; }
</style>
</head>
<body>
<div class="wizard" id="app">
  <h1>🚀 ContainerCP Setup</h1>
  <p class="sub">Configure your hosting control panel</p>

  <div id="step1" class="step">
    <div><span class="step-num">1</span><span class="step-title">Server Hostname</span></div>
    <p style="font-size:12px;color:#a1a1aa;margin:8px 0 12px;">Enter the domain where your admin panel will be accessible.</p>
    <div class="field">
      <label>Hostname</label>
      <input id="hostname" placeholder="panel.example.com" oninput="document.getElementById('s1-status').style.display='none'">
    </div>
    <button class="btn btn-primary" onclick="saveHostname()">Save &amp; Continue</button>
    <div id="s1-status" class="status"></div>
  </div>

  <div id="step2" class="step hidden">
    <div><span class="step-num">2</span><span class="step-title">Finish</span></div>
    <p style="font-size:12px;color:#a1a1aa;margin:8px 0 12px;">Setup is complete. The daemon will restart in normal mode.<br>After restart, go to Settings → Admin Panel HTTPS to configure SSL.</p>
    <button class="btn btn-primary" onclick="completeSetup()">Finish Setup</button>
    <div id="s2-status" class="status"></div>
  </div>
</div>
<script>
async function api(path, opts) {
  const res = await fetch(path, opts);
  return await res.json();
}
function $(id) { return document.getElementById(id); }
function status(id, msg, type) {
  const el = $(id);
  el.textContent = msg;
  el.className = 'status ' + type;
}

async function saveHostname() {
  const h = $('hostname').value.trim();
  if (!h) { status('s1-status', 'Enter a hostname', 'err'); return; }
  status('s1-status', 'Saving...', 'info');
  const res = await api('/api/bootstrap/save-hostname', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({hostname: h})
  });
  if (res.success) {
    status('s1-status', 'Hostname saved: ' + h, 'ok');
    $('step1').classList.add('hidden');
    $('step2').classList.remove('hidden');
    completeSetup();
  } else {
    status('s1-status', res.error || 'Failed to save', 'err');
  }
}

async function completeSetup() {
  status('s2-status', 'Finalizing...', 'info');
  const res = await api('/api/bootstrap/complete', {method:'POST'});
  if (res.success) {
    status('s2-status', 'Setup complete! Restarting daemon...', 'ok');
    setTimeout(() => { location.reload(); }, 2000);
  } else {
    status('s2-status', res.error || 'Failed to complete setup', 'err');
  }
}

</script>
</body>
</html>
)HTML";

// Very simple HTTP response helpers
static std::string http_ok(const std::string& body, const std::string& content_type = "text/html") {
    std::ostringstream r;
    r << "HTTP/1.1 200 OK\r\n"
      << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << body;
    return r.str();
}

static std::string http_json_ok(const std::string& json_data) {
    return http_ok(json_data, "application/json");
}

static std::string http_json_error(const std::string& msg) {
    return http_ok("{\"success\":false,\"error\":\"" + msg + "\"}", "application/json");
}

static std::string http_404() {
    return http_ok("<h1>404 Not Found</h1>", "text/html");
}

// JSON extraction helper
static std::string json_extract(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        auto end = json.find_first_of(",}", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }
    pos += search.size();
    auto end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

int containercp::core::StartupManager::run_bootstrap(const std::string& data_root) {
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[BOOTSTRAP] Failed to create socket" << std::endl;
        return 1;
    }
    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = ::htons(80);

    if (::bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[BOOTSTRAP] Failed to bind port 80" << std::endl;
        ::close(server_fd);
        return 1;
    }
    ::listen(server_fd, 5);
    std::cerr << "[BOOTSTRAP] Listening on http://0.0.0.0:80/" << std::endl;
    std::cerr << "[BOOTSTRAP] Open Setup Wizard in your browser." << std::endl;

    bool running = true;
    while (running) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) break;

        char buf[8192];
        ssize_t n = ::read(client_fd, buf, sizeof(buf) - 1);
        std::string response;

        if (n > 0) {
            buf[n] = '\0';
            std::string req(buf);
            std::string method, path;
            {
                std::istringstream ss(req);
                ss >> method >> path;
            }

            if (method == "GET" && path == "/") {
                response = http_ok(SETUP_HTML);
            } else if (method == "GET" && path == "/api/bootstrap/status") {
                response = http_json_ok("{\"success\":true,\"data\":{\"mode\":\"bootstrap\"}}");
            } else if (method == "POST" && path == "/api/bootstrap/save-hostname") {
                auto body_pos = req.find("\r\n\r\n");
                std::string body = (body_pos != std::string::npos) ? req.substr(body_pos + 4) : "";
                std::string hostname = json_extract(body, "hostname");
                if (hostname.empty()) {
                    response = http_json_error("Hostname is required");
                } else {
                    std::string path = data_root + "/server_hostname";
                    std::ofstream f(path);
                    if (f.is_open()) f << hostname;
                    response = http_json_ok("{\"success\":true,\"data\":{\"hostname\":\"" + hostname + "\"}}");
                }
            } else if (method == "POST" && path == "/api/bootstrap/issue-ssl") {
                // For bootstrap, SSL is issued through the normal ACME flow
                // but we just acknowledge it here. The actual ACME call will
                // happen when the daemon starts in normal mode.
                auto body_pos = req.find("\r\n\r\n");
                std::string body = (body_pos != std::string::npos) ? req.substr(body_pos + 4) : "";
                std::string hostname = json_extract(body, "hostname");
                if (hostname.empty()) {
                    response = http_json_error("Hostname is required");
                } else {
                    response = http_json_ok("{\"success\":true,\"data\":{\"message\":\"SSL will be configured on first normal startup\"}}");
                }
            } else if (method == "POST" && path == "/api/bootstrap/complete") {
                mark_setup_completed(data_root);
                response = http_json_ok("{\"success\":true,\"data\":{\"message\":\"Setup complete. Switching to normal mode...\"}}");
                running = false; // exit the accept loop, systemd will restart
            } else {
                response = http_404();
            }
        }

        if (!response.empty()) {
            ::write(client_fd, response.data(), response.size());
        }
        ::close(client_fd);
    }

    ::close(server_fd);
    // Exit with non-zero so systemd (Restart=on-failure) restarts into Normal Mode
    return 99;
}
