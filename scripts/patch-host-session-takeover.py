#!/usr/bin/env python3
"""Add an authenticated loopback admission API to the pinned Sunshine sources."""
import difflib
import subprocess
import sys
from pathlib import Path
root = Path(sys.argv[1])
marker = root / 'deskport-session-takeover.patch'
if marker.exists():
    subprocess.run(['git', 'apply', '--reverse', '--check', str(marker.resolve())], cwd=root, check=True)
    if '--revert' in sys.argv:
        subprocess.run(['git', 'apply', '--reverse', str(marker.resolve())], cwd=root, check=True)
        marker.unlink()
    raise SystemExit(0)
if '--revert' in sys.argv:
    raise SystemExit(0)
original, changes = {}, {}
def edit(name, anchor, replacement):
    path = root / 'src' / name
    source = changes.get(path, path.read_text())
    original.setdefault(path, source)
    if source.count(anchor) != 1:
        raise SystemExit(f'Expected one takeover anchor in {name}: {anchor!r}')
    changes[path] = source.replace(anchor, replacement, 1)
edit('rtsp.h', '#pragma once', '''#pragma once
#include <mutex>
#include <string>
// Shared by the management API, authenticated launch/resume and RTSP admission.
namespace deskport_admission {
  inline std::recursive_mutex mutex;
  inline std::string certificate, lease;
  inline unsigned long long generation = 0;
}''')
edit('rtsp.h', '    bool host_audio;', '    unsigned long long deskport_generation = 0;\n    bool host_audio;')
if 'task_pool.start(1)' not in (root / 'src/main.cpp').read_text():
    raise SystemExit('Input fence requires the pinned single-worker task pool')
# Bind identity to the verified TLS request, not a thread-local last handshake.
# The older Linux pin has no upstream per-session certificate field.
edit('rtsp.h', '    bool host_audio;', '    std::string deskport_certificate;\n    bool host_audio;')
edit('nvhttp.cpp', '    std::function<int(SSL *)> verify;', '''    inline static std::map<const Request*, std::pair<std::weak_ptr<Request>, std::string>> deskport_identities;
    static std::string deskport_identity(const std::shared_ptr<Request>& request) {
      std::lock_guard lock {deskport_admission::mutex};
      auto found = deskport_identities.find(request.get());
      return found != deskport_identities.end() && found->second.first.lock() == request ? found->second.second : std::string();
    }
    std::function<int(SSL *)> verify;''')
edit('nvhttp.cpp', '                this->read(session);', '''                {
                  std::lock_guard lock {deskport_admission::mutex};
                  for (auto it = deskport_identities.begin(); it != deskport_identities.end();) {
                    if (it->second.first.expired()) it = deskport_identities.erase(it); else ++it;
                  }
                  crypto::x509_t certificate {SSL_get_peer_certificate(session->connection->socket->native_handle())};
                  if (certificate) deskport_identities[session->request.get()] = {session->request, crypto::pem(certificate)};
                }
                this->read(session);''')
edit('nvhttp.cpp', 'make_launch_session(bool host_audio, const args_t &args)',
     'make_launch_session(bool host_audio, const args_t &args, const std::string& deskport_certificate)')
edit('nvhttp.cpp', '    launch_session->host_audio = host_audio;', '''    launch_session->host_audio = host_audio;
    launch_session->deskport_certificate = deskport_certificate;
    launch_session->deskport_generation = deskport_admission::generation;''')
path = root / 'src/nvhttp.cpp'
source = changes[path]
if source.count('make_launch_session(host_audio, args)') != 2:
    raise SystemExit('Expected both authenticated launch/resume paths')
changes[path] = source.replace('make_launch_session(host_audio, args)', 'make_launch_session(host_audio, args, SunshineHTTPSServer::deskport_identity(request))')
if 'get_cert_by_uuid(' not in (root / 'src/nvhttp.h').read_text():
    edit('nvhttp.h', '  nlohmann::json get_all_clients();', '  nlohmann::json get_all_clients();\n  std::string get_cert_by_uuid(std::string_view uuid);')
    edit('nvhttp.cpp', '  nlohmann::json get_all_clients() {', '''  std::string get_cert_by_uuid(std::string_view uuid) {
    for (const auto& named_cert : client_root.named_devices) {
      if (named_cert.uuid == uuid && named_cert.enabled) {
        auto certificate = crypto::x509(named_cert.cert);
        if (certificate) return crypto::pem(certificate);
      }
    }
    return {};
  }

  nlohmann::json get_all_clients() {''')
# Serialize the entire authenticated launch/resume/cancel handler with management.
for handler in ['launch', 'resume', 'cancel']:
    path = root / 'src/nvhttp.cpp'
    source = changes.get(path, path.read_text())
    start = source.index('  void ' + handler + '(')
    end = source.index('\n  }', start)
    body = source[start:end]
    anchor = '    auto args = request->parse_query_string();' if handler == 'launch' else ('    auto current_appid = proc::proc.running();' if handler == 'resume' else '    tree.put("root.cancel", 1);')
    gate = '''    std::lock_guard admission_lock {deskport_admission::mutex};
    const auto requesting_certificate = SunshineHTTPSServer::deskport_identity(request);
    if (requesting_certificate.empty()) {
      tree.put("root.<xmlattr>.status_code", 401);
      return;
    }
    if (!deskport_admission::certificate.empty() && deskport_admission::certificate != requesting_certificate) {
      tree.put("root.<xmlattr>.status_code", 409);
      tree.put("root.<xmlattr>.status_message", "Another device owns this DeskPort session");
      return;
    }
'''
    if handler != 'cancel':
        gate += '''    if (rtsp_stream::session_count() != 0) {
      tree.put("root.<xmlattr>.status_code", 409);
      tree.put("root.<xmlattr>.status_message", "The previous stream is still active");
      return;
    }
'''
    gate += '    ++deskport_admission::generation;\n'
    edit('nvhttp.cpp', body, body.replace(anchor, gate + anchor, 1))
# Old RTSP negotiations cannot enter after a claim, even from the same identity.
edit('rtsp.cpp', '    auto stream_session = stream::session::alloc(config, session);', '''    std::lock_guard admission_lock {deskport_admission::mutex};
    if (session.deskport_generation != deskport_admission::generation ||
        (!deskport_admission::certificate.empty() && deskport_admission::certificate != session.deskport_certificate) ||
        server->session_count() != 0) {
      respond(sock, session, &option, 409, "Session ownership changed", req->sequenceNumber, {});
      return;
    }
    auto stream_session = stream::session::alloc(config, session);''')
edit('rtsp.cpp', '    server.clear(true);', '    server.launch_event.pop(0s);\n    server.clear(true);')
edit('confighttp.cpp', '  void closeApp(const resp_https_t &response, const req_https_t &request) {', '''  // Machine-only API: loopback, explicit Basic credentials, no browser origins.
  // It never changes pairing records, restarts Sunshine or terminates desktop apps.
  void deskportSessions(const resp_https_t &response, const req_https_t &request) {
    auto authorization = request->header.find("Authorization");
    if (!request->remote_endpoint().address().is_loopback() ||
        authorization == request->header.end() || authorization->second.rfind("Basic ", 0) != 0 ||
        request->header.find("Origin") != request->header.end() ||
        request->header.find("Referer") != request->header.end() || !authenticate(response, request)) {
      bad_request(response, request, "Authenticated local management only");
      return;
    }
    std::lock_guard admission_lock {deskport_admission::mutex};
    auto snapshot = [] {
      return std::to_string(deskport_admission::generation) + ":" + std::to_string(rtsp_stream::session_count());
    };
    nlohmann::json output;
    if (request->method == "POST") {
      try {
        const auto input = nlohmann::json::parse(request->content.string());
        const auto action = input.value("action", "");
        if (action == "release") {
          if (!deskport_admission::lease.empty() && input.value("lease", "") == deskport_admission::lease) {
            deskport_admission::certificate.clear(); deskport_admission::lease.clear();
            ++deskport_admission::generation;
          }
          output["status"] = true;
        } else if (action == "acquire") {
          const auto cert = nvhttp::get_cert_by_uuid(input.value("uuid", ""));
          const auto lease = input.value("lease", "");
          if (cert.empty() || lease.empty() || lease.size() > 64) {
            output["status"] = false; output["code"] = "unauthorized";
          } else if (input.value("snapshot", "") != snapshot()) {
            output["status"] = false; output["code"] = "stale";
          } else if (!input.value("takeover", false) &&
                     (rtsp_stream::session_count() != 0 || !deskport_admission::lease.empty())) {
            output["status"] = false; output["code"] = "busy";
          } else {
            // Fence pending RTSP negotiations before joining every old stream.
            ++deskport_admission::generation;
            deskport_admission::certificate = cert; deskport_admission::lease = lease;
            rtsp_stream::terminate_sessions();
            const bool input_released = task_pool.push([] {}).wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            output["status"] = input_released && rtsp_stream::session_count() == 0;
            if (!input_released) output["code"] = "unavailable";
          }
        } else { output["status"] = false; output["code"] = "unavailable"; }
      } catch (const std::exception &) {
        output["status"] = false; output["code"] = "unavailable";
      }
    } else output["status"] = true;
    output["version"] = 1;
    output["snapshot"] = snapshot();
    output["sessions"] = rtsp_stream::session_count();
    output["reserved"] = !deskport_admission::lease.empty();
    send_response(response, output);
  }

  void closeApp(const resp_https_t &response, const req_https_t &request) {''')
edit('confighttp.cpp', '    server.resource["^/api/apps/close$"]["POST"] = closeApp;', '''    server.resource["^/api/apps/close$"]["POST"] = closeApp;
    server.resource["^/api/deskport/sessions$"]["GET"] = deskportSessions;
    server.resource["^/api/deskport/sessions$"]["POST"] = deskportSessions;''')
patch = ''.join(''.join(difflib.unified_diff(original[path].splitlines(True), updated.splitlines(True),
    fromfile='a/' + str(path.relative_to(root)), tofile='b/' + str(path.relative_to(root))))
    for path, updated in changes.items())
marker.write_text(patch)
subprocess.run(['git', 'apply', '--check', str(marker.resolve())], cwd=root, check=True)
subprocess.run(['git', 'apply', str(marker.resolve())], cwd=root, check=True)
