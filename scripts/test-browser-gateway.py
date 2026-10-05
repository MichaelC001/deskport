#!/usr/bin/env python3
"""Compile and exercise only an isolated loopback HTTPS gateway with a fake host."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import http.client
import hashlib
import json
import os
from pathlib import Path
import re
import select
import socket
import ssl
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=Path.home() / "mygit/build/deskport/browser-client/pairing")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix="test-", dir=args.output))
project = work / "test.pro"
resources = work / "resources.qrc"
(work / "index.html").write_text("<!doctype html><title>Isolated DeskPort</title>")
(work / "app.js").write_text("'use strict';")
(work / "style.css").write_text("body { color: black; }")
resources.write_text('<RCC><qresource prefix="/browser">' + ''.join(
    f'<file alias="{name}">{work / name}</file>' for name in ("index.html", "app.js", "style.css")) + '</qresource></RCC>')
project.write_text(f'''QT = core network
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += openssl
TARGET = gateway-test
SOURCES += "{ROOT}/tests/browser-gateway.cpp" "{ROOT}/app/backend/browsergateway.cpp"
HEADERS += "{ROOT}/app/backend/browsergateway.h"
INCLUDEPATH += "{ROOT}/app/backend"
RESOURCES += "{resources}"
''')
with (work / "build.log").open("w") as log:
    subprocess.run([os.environ.get("DESKPORT_QMAKE", "qmake"), str(project)], cwd=work, check=True, stdout=log, stderr=log)
    build = subprocess.run(["make", "-j2"], cwd=work, stdout=log, stderr=log)
if build.returncode:
    print((work / "build.log").read_text())
    raise SystemExit(build.returncode)

processes = []
checks = []
def check(condition, label):
    if not condition:
        raise AssertionError(label)
    checks.append(label)

def line(process):
    ready, _, _ = select.select([process.stdout], [], [], 20)
    if not ready:
        raise RuntimeError("Isolated test process did not respond")
    value = process.stdout.readline()
    if not value:
        raise RuntimeError("Isolated test process exited")
    return json.loads(value)

def launch(directory, port=0, certificate=None, key=None, environment=None):
    command = [str(work / "gateway-test"), str(directory), str(port)]
    if certificate is not None:
        command += [str(certificate), str(key)]
    process = subprocess.Popen(command, cwd=work, env=dict(os.environ, **(environment or {})),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    processes.append(process)
    info = line(process)
    return process, info

def command(process, value):
    process.stdin.write(value.encode() + b"\n")
    process.stdin.flush()
    return line(process)

def finish(process):
    if process.poll() is None:
        process.stdin.write(b"quit\n")
        process.stdin.flush()
        process.wait(timeout=10)

def request(info, directory, method, path, body=None, auth=None, headers=None, raw=False, certificate=None, hostname="127.0.0.1"):
    context = ssl.create_default_context(cafile=str(certificate or directory / "https-cert.pem"))
    connection = http.client.HTTPSConnection(hostname, info["port"], context=context, timeout=10)
    if hostname != "127.0.0.1":
        # Resolve only this fixture to loopback while preserving the real DNS
        # hostname for TLS verification, SNI, Host and same-origin checks.
        connection._create_connection = lambda address, timeout=10, source_address=None: socket.create_connection(
            ("127.0.0.1", address[1]), timeout, source_address)
    values = {"Origin": f'https://{hostname}:{info["port"]}'}
    if method == "POST":
        values["Content-Type"] = "application/json"
    if auth:
        values.update({"Cookie": auth[0], "X-DeskPort-Session": auth[1]})
    values.update(headers or {})
    payload = json.dumps(body if body is not None else {}) if method == "POST" else None
    connection.request(method, path, body=payload, headers=values)
    response = connection.getresponse()
    data = response.read()
    response_headers = dict(response.getheaders())
    response_headers["Set-Cookies"] = response.headers.get_all("Set-Cookie", [])
    result = data if raw else json.loads(data)
    status = response.status
    connection.close()
    return status, result, response_headers

def login(info, directory, code=None):
    status, result, headers = request(info, directory, "POST", "/api/login", {"code": code or info["code"]})
    check(status == 200 and result.get("ok") and "csrfToken" in result, "valid permanent code authenticates")
    cookie = headers["Set-Cookie"]
    check(all(value in cookie for value in ("Secure", "HttpOnly", "SameSite=Strict", "Path=/")), "cookie has secure session attributes")
    check("Max-Age" not in cookie and "Expires" not in cookie, "login cookie is not persistent")
    return cookie.split(";", 1)[0], result["csrfToken"]

def cookies(headers):
    return {value.split("=", 1)[0]: value for value in headers["Set-Cookies"]}

def pairing_login(info, directory, name="Isolated Safari", existing=""):
    status, result, headers = request(info, directory, "POST", "/api/login",
        {"code": info["code"], "remember": True, "deviceName": name}, headers={"Cookie": existing})
    check(status == 200 and result.get("paired") and result["pairing"]["name"] == name,
        "remembered login returns public device identity")
    values = cookies(headers)
    check(len(values) == 2 and all(value in values["__Host-deskport-pairing"] for value in
        ("Secure", "HttpOnly", "SameSite=Strict", "Path=/", "Max-Age=31536000")),
        "pairing and session use distinct Set-Cookie headers with persistent pairing attributes")
    pair = values["__Host-deskport-pairing"].split(";", 1)[0]
    session = values["__Host-deskport-session"].split(";", 1)[0]
    return (session + "; " + pair, result["csrfToken"]), pair, result["pairing"]["id"]

def resume(info, directory, pair):
    status, result, headers = request(info, directory, "POST", "/api/resume", {}, headers={"Cookie": pair})
    check(status == 200 and result.get("paired") and result.get("csrfToken"), "pairing restores a fresh short session")
    values = cookies(headers)
    check(len(values) == 2 and "Max-Age=31536000" in values["__Host-deskport-pairing"] and
        "Max-Age" not in values["__Host-deskport-session"], "resume refreshes pairing lifetime without persisting short session")
    return (values["__Host-deskport-session"].split(";", 1)[0] + "; " + pair, result["csrfToken"]), result

try:
    directory = work / "state"
    process, info = launch(directory)
    check("error" not in info, "gateway starts on isolated loopback")
    check(bool(re.fullmatch(r"[0-9A-HJKMNP-TV-Z]{6}", info["code"])), "host creates six-character alphanumeric code")
    check(directory.stat().st_mode & 0o777 == 0o700, "settings directory is owner-only")
    for name in ("browser.ini", "https-key.pem", "https-cert.pem"):
        check((directory / name).stat().st_mode & 0o777 == 0o600, f"{name} is owner-only")
    status, page, headers = request(info, directory, "GET", "/", raw=True)
    check(status == 200 and b"Isolated DeskPort" in page, "trusted TLS certificate has loopback SAN and serves embedded page")
    check(info["code"].encode() not in page, "anonymous page does not expose access code")
    check("frame-ancestors 'none'" in headers["Content-Security-Policy"], "static page has restrictive CSP")
    check(request(info, directory, "GET", "/api/status")[0] == 401, "status requires authentication")
    check(request(info, directory, "POST", "/api/login", {"code": info["code"]}, headers={"Origin": "https://evil.invalid"})[0] == 403, "cross-origin login is refused")
    check(request(info, directory, "POST", "/api/login", {"code": info["code"]}, headers={"Origin": ""})[0] == 403, "missing-origin mutation is refused")
    check(request(info, directory, "GET", "/", headers={"Host": f'evil.invalid:{info["port"]}'})[0] == 400, "unknown Host is refused")
    check(request(info, directory, "POST", "/api/login", {"code": "!!!!!!"})[0] == 401, "incorrect code is refused")
    auth = login(info, directory, info["code"].lower())
    check(request(info, directory, "GET", "/api/status", headers={"Cookie": auth[0]})[0] == 401, "cookie alone cannot restore a new page session")
    check(request(info, directory, "GET", "/api/status", auth=(auth[0], "wrong"))[0] == 401, "incorrect in-memory session header is refused")
    status, state, _ = request(info, directory, "GET", "/api/status", auth=auth)
    check(status == 200 and state["sharing"] and "privateSecret" not in state, "authenticated status uses explicit public fields")
    check(info["code"] not in json.dumps(state), "authenticated status does not expose access code")
    check(request(info, directory, "POST", "/api/session/start", {"id": "other"}, auth=auth)[0] == 400, "browser cannot choose media identity")
    status, offer, _ = request(info, directory, "POST", "/api/session/start", {"width": 1280, "height": 720}, auth=auth)
    check(status == 200 and offer["type"] == "offer" and "id" not in offer and "lease" not in offer, "server offer is proxied without internal session identity")
    snapshot = command(process, "snapshot")
    check(snapshot["last"]["id"].startswith("browser-") and snapshot["starts"] == 1, "gateway assigns media identity")
    check(bool(re.fullmatch(r"[A-Za-z0-9-]{16,64}", snapshot["last"]["id"])), "generated media identity satisfies the native admission contract")
    check(request(info, directory, "POST", "/api/session/start", {}, auth=auth)[0] == 409, "duplicate start is refused")
    check(request(info, directory, "POST", "/api/session/answer", {"type": "answer", "sdp": "v=0"}, auth=auth)[0] == 200, "answer reaches host adapter")
    check(request(info, directory, "GET", "/api/session/status", auth=auth)[0] == 200, "media status reports session state")
    check(request(info, directory, "POST", "/api/session/heartbeat", {}, auth=auth)[0] == 200, "explicit heartbeat reaches media adapter")
    check(request(info, directory, "POST", "/api/logout", {}, auth=auth)[0] == 200, "logout succeeds")
    check(command(process, "snapshot")["stops"] == 1, "logout releases media")
    check(request(info, directory, "GET", "/api/status", auth=auth)[0] == 401, "logged-out session cannot replay")
    auth = login(info, directory)
    command(process, "off")
    check(request(info, directory, "POST", "/api/session/start", {}, auth=auth)[0] == 409, "sharing-off rejects media start")
    command(process, "on")
    check(request(info, directory, "POST", "/api/session/start", {}, auth=auth)[0] == 200, "sharing enabled permits start")
    command(process, "off")
    check(command(process, "snapshot")["stops"] == 2, "turning sharing off releases existing media")
    command(process, "on")
    request(info, directory, "POST", "/api/session/start", {}, auth=auth)
    time.sleep(1.2)
    request(info, directory, "POST", "/api/session/heartbeat", {}, auth=auth)
    time.sleep(1.2)
    check(command(process, "snapshot")["active"], "explicit heartbeat preserves active media")
    request(info, directory, "GET", "/api/session/status", auth=auth)
    time.sleep(3.2)
    check(command(process, "snapshot")["stops"] == 3, "missing browser heartbeat releases media")
    check(request(info, directory, "GET", "/api/status", auth=auth)[0] == 401, "heartbeat expiry requires fresh authentication")
    original_code = info["code"]
    original_cert = (directory / "https-cert.pem").read_bytes()
    finish(process)
    process, info = launch(directory)
    check(info["code"] == original_code and (directory / "https-cert.pem").read_bytes() == original_cert, "code and HTTPS identity survive process restart")
    auth = login(info, directory)
    request(info, directory, "POST", "/api/session/start", {}, auth=auth)
    new = command(process, "reset")
    check(new["code"] != info["code"] and new["stops"] == 1, "explicit code reset releases media and replaces credential")
    check(request(info, directory, "GET", "/api/status", auth=auth)[0] == 401, "reset invalidates old login sessions")
    wrong_results = [request(info, directory, "POST", "/api/login", {"code": original_code})[0] for _ in range(6)]
    check(401 in wrong_results and 429 in wrong_results, "wrong permanent codes are rate limited")
    finish(process)
    busy = socket.socket()
    busy.bind(("127.0.0.1", 0)); busy.listen()
    conflict, result = launch(work / "conflict", busy.getsockname()[1])
    check("error" in result and "Existing services were left unchanged" in result["error"], "occupied port is an explicit failure")
    conflict.wait(timeout=5); busy.close()
    partial, result = launch(work / "partial", environment={"DESKPORT_TEST_PARTIAL_BIND": "1"})
    check("error" in result and not result["active"] and result["port"] == 0 and result["urlCount"] == 0,
        "partial listener failure rolls back every listener and advertised URL")
    released_port = int(re.search(r"127\.0\.0\.1:([0-9]+):", result["error"])[1])
    probe = socket.socket(); probe.bind(("127.0.0.1", released_port)); probe.close()
    check(True, "failed multi-listener startup releases its first bound socket")
    partial.wait(timeout=5)
    corrupt_dir = work / "corrupt"
    corrupt_dir.mkdir()
    (corrupt_dir / "browser.ini").write_text("[access]\ncode=invalid\n")
    corrupt, result = launch(corrupt_dir)
    check("error" in result and "code=invalid" in (corrupt_dir / "browser.ini").read_text(),
        "invalid saved access code fails without silent replacement")
    corrupt.wait(timeout=5)
    incomplete_dir = work / "incomplete"
    incomplete_dir.mkdir()
    preserved_key = (directory / "https-key.pem").read_bytes()
    (incomplete_dir / "https-key.pem").write_bytes(preserved_key)
    incomplete, result = launch(incomplete_dir)
    check("error" in result and (incomplete_dir / "https-key.pem").read_bytes() == preserved_key and
        not (incomplete_dir / "https-cert.pem").exists(), "incomplete certificate pair fails without replacing existing key")
    incomplete.wait(timeout=5)
    missing, result = launch(work / "missing", certificate=work / "missing.pem", key=work / "missing.key")
    check("error" in result, "invalid custom HTTPS credentials fail closed")
    missing.wait(timeout=5)
    custom, result = launch(work / "custom", certificate=directory / "https-cert.pem", key=directory / "https-key.pem")
    check("error" not in result and request(result, work / "custom", "GET", "/", raw=True,
        certificate=directory / "https-cert.pem")[0] == 200, "custom certificate and key serve trusted HTTPS")
    finish(custom)
    dns_cert, dns_key = work / "dns-cert.pem", work / "dns-key.pem"
    dns_name = "browser.example.test"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
        "-subj", f"/CN={dns_name}", "-addext", f"subjectAltName=DNS:{dns_name}",
        "-keyout", str(dns_key), "-out", str(dns_cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dns, dns_info = launch(work / "dns", certificate=dns_cert, key=dns_key,
        environment={"DESKPORT_TEST_ALLOWED_HOSTS": dns_name})
    check(dns_info["urls"][0] == f'https://{dns_name}:{dns_info["port"]}/', "explicit custom-certificate hostname is the preferred URL")
    check(request(dns_info, work / "dns", "POST", "/api/login", {"code": dns_info["code"]},
        certificate=dns_cert, hostname=dns_name)[0] == 200, "custom DNS certificate and same-origin login validate over real TLS")
    finish(dns)
    race_dir = work / "race"
    race, race_info = launch(race_dir)
    race_auth = login(race_info, race_dir)
    command(race, "defer")
    with ThreadPoolExecutor(max_workers=1) as executor:
        pending = executor.submit(request, race_info, race_dir, "POST", "/api/session/start", {}, race_auth)
        time.sleep(0.15)
        check(command(race, "snapshot")["active"], "deferred start holds an isolated test lease")
        check(request(race_info, race_dir, "POST", "/api/logout", {}, auth=race_auth)[0] == 200, "logout can cancel an in-flight start")
        command(race, "complete")
        check(pending.result(timeout=5)[0] == 409 and not command(race, "snapshot")["active"], "late start cannot resurrect a logged-out session")
    race_auth = login(race_info, race_dir)
    command(race, "defer")
    context = ssl.create_default_context(cafile=str(race_dir / "https-cert.pem"))
    connection = http.client.HTTPSConnection("127.0.0.1", race_info["port"], context=context, timeout=5)
    connection.request("POST", "/api/session/start", body="{}", headers={"Origin": f'https://127.0.0.1:{race_info["port"]}',
        "Content-Type": "application/json", "Cookie": race_auth[0], "X-DeskPort-Session": race_auth[1]})
    time.sleep(0.15)
    check(command(race, "snapshot")["active"], "second deferred start acquires a test lease")
    connection.close()
    time.sleep(0.15)
    command(race, "complete")
    check(not command(race, "snapshot")["active"], "closed HTTP start rolls back late media success")
    finish(race)
    pair_dir = work / "paired"
    paired, pair_info = launch(pair_dir)
    status, result, headers = request(pair_info, pair_dir, "POST", "/api/resume")
    check(status == 401 and result["code"] == "unpaired" and not headers["Set-Cookies"],
        "missing pairing returns unpaired without mutating shared browser cookies")
    status, result, headers = request(pair_info, pair_dir, "POST", "/api/login", {"code": pair_info["code"], "remember": False})
    check(status == 200 and result["paired"] is False and result["pairing"] is None and len(headers["Set-Cookies"]) == 1,
        "temporary login remains available without device enrollment")
    auth1, pair_cookie, pair_id = pairing_login(pair_info, pair_dir)
    pairing_file = pair_dir / "pairings.json"
    saved = json.loads(pairing_file.read_text())
    saved_row = saved["devices"][0]
    raw_token = pair_cookie.split("=", 1)[1]
    check(pairing_file.stat().st_mode & 0o777 == 0o600 and saved_row["hash"] == hashlib.sha256(raw_token.encode()).hexdigest() and
        raw_token not in pairing_file.read_text(), "private pairing file persists only the credential hash")
    snapshot = command(paired, "snapshot")
    check(len(snapshot["pairings"]) == 1 and set(snapshot["pairings"][0]) == {"id", "name", "createdAt", "lastSeenAt"},
        "local pairing list exposes only public metadata")
    check(request(pair_info, pair_dir, "GET", "/api/status", headers={"Cookie": pair_cookie})[0] == 401,
        "device cookie does not bypass short-session CSRF authentication")
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie, "Origin": "https://evil.invalid"})[0] == 403,
        "cross-origin resume is refused")
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie, "Origin": ""})[0] == 403,
        "resume requires an explicit same-origin request")
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie + "; " + pair_cookie})[0] == 401,
        "ambiguous duplicate pairing cookies are refused")
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": "__Host-deskport-pairing=; " + pair_cookie})[0] == 401,
        "empty-first duplicate pairing cookies are also refused")
    auth2, resumed = resume(pair_info, pair_dir, pair_cookie)
    check(resumed["pairing"]["id"] == pair_id and auth2[1] != auth1[1], "resume retains device identity and issues fresh CSRF token")
    check(request(pair_info, pair_dir, "POST", "/api/session/start", {}, auth=auth2)[0] == 200, "resumed device can start media")
    status, tab_result, tab_headers = request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": auth2[0]})
    check(status == 200 and tab_result["csrfToken"] == auth2[1] and command(paired, "snapshot")["active"] and
        request(pair_info, pair_dir, "POST", "/api/session/heartbeat", {}, auth=auth2)[0] == 200,
        "automatic resume in a second tab preserves the active shared session and CSRF token")
    status, _, headers = request(pair_info, pair_dir, "POST", "/api/logout", {}, auth=auth1)
    check(status == 200 and len(headers["Set-Cookies"]) == 1 and command(paired, "snapshot")["active"],
        "logout removes only its short session and preserves device pairing and other sessions")
    check(request(pair_info, pair_dir, "POST", "/api/session/stop", {}, auth=auth2)[0] == 200 and len(command(paired, "snapshot")["pairings"]) == 1,
        "media stop preserves persistent pairing")
    finish(paired)
    paired, pair_info = launch(pair_dir)
    auth3, resumed = resume(pair_info, pair_dir, pair_cookie)
    check(resumed["pairing"]["id"] == pair_id and json.loads(pairing_file.read_text())["devices"][0]["lastSeenAt"] >= saved_row["lastSeenAt"],
        "paired credential survives server restart and refreshes last-seen metadata")
    auth4, _, reused_id = pairing_login(pair_info, pair_dir, existing=pair_cookie)
    check(reused_id == pair_id and len(command(paired, "snapshot")["pairings"]) == 1, "remembered re-login reuses its device instead of duplicating enrollment")
    check(request(pair_info, pair_dir, "POST", "/api/forget", auth=(auth3[0], "wrong"))[0] == 401,
        "forget requires current session CSRF token")
    check(request(pair_info, pair_dir, "POST", "/api/forget", auth=auth3, headers={"Origin": "https://evil.invalid"})[0] == 403,
        "cross-origin forget is refused")
    request(pair_info, pair_dir, "POST", "/api/session/start", {}, auth=auth4)
    revoked = command(paired, "revoke " + pair_id)
    check(revoked["revoked"] and not revoked["active"] and not revoked["pairings"], "local revocation stops active media and removes the device")
    check(all(request(pair_info, pair_dir, "GET", "/api/status", auth=auth)[0] == 401 for auth in (auth3, auth4)),
        "revocation invalidates every short session associated with a device")
    status, result, stale_headers = request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie})
    check(status == 401 and result["code"] == "unpaired" and not stale_headers["Set-Cookies"],
        "revoked device credential cannot replay or mutate shared browser cookies")
    finish(paired)
    paired, pair_info = launch(pair_dir)
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie})[0] == 401,
        "revocation remains effective across server restart")
    auth5, pair_cookie2, pair_id2 = pairing_login(pair_info, pair_dir)
    check(pair_cookie2 != pair_cookie and not stale_headers["Set-Cookies"],
        "a delayed invalid resume response has no cookie mutation that could erase a newer pairing")
    auth6, _ = resume(pair_info, pair_dir, pair_cookie2)
    request(pair_info, pair_dir, "POST", "/api/session/start", {}, auth=auth5)
    status, _, headers = request(pair_info, pair_dir, "POST", "/api/forget", {}, auth=auth6)
    check(status == 200 and len(headers["Set-Cookies"]) == 2 and all("Max-Age=0" in value for value in headers["Set-Cookies"]),
        "forget clears both pairing and short-session cookies with separate headers")
    check(not command(paired, "snapshot")["active"] and request(pair_info, pair_dir, "GET", "/api/status", auth=auth5)[0] == 401 and
        request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie2})[0] == 401,
        "forget terminates other sessions and permanently revokes the credential")
    auth7, pair_cookie3, pair_id3 = pairing_login(pair_info, pair_dir)
    other_auth, other_cookie, other_id = pairing_login(pair_info, pair_dir, "Second browser")
    command(paired, "defer")
    with ThreadPoolExecutor(max_workers=1) as executor:
        pending = executor.submit(request, pair_info, pair_dir, "POST", "/api/session/start", {}, auth7)
        time.sleep(0.15)
        check(command(paired, "revoke " + pair_id3)["revoked"], "pending media can be revoked")
        command(paired, "complete")
        check(pending.result(timeout=5)[0] == 409 and not command(paired, "snapshot")["active"],
            "late media success cannot resurrect a revoked pairing")
    check(request(pair_info, pair_dir, "GET", "/api/status", auth=other_auth)[0] == 200 and
        request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": other_cookie})[0] == 200,
        "revoking one browser preserves another browser's pairing and short session")
    finish(paired)
    write_dir = work / "write-failure"
    writer, write_info = launch(write_dir)
    write_auth, write_cookie, write_id = pairing_login(write_info, write_dir)
    request(write_info, write_dir, "POST", "/api/session/start", {}, auth=write_auth)
    blocked_file = write_dir / "pairings.json"
    backup_file = write_dir / "pairings.saved"
    blocked_file.rename(backup_file); blocked_file.mkdir()
    failed = command(writer, "revoke " + write_id)
    check(not failed["revoked"] and failed["active"] and len(failed["pairings"]) == 1,
        "failed durable revoke preserves the live credential and does not claim success")
    status, result, headers = request(write_info, write_dir, "POST", "/api/forget", auth=write_auth)
    check(status == 503 and result["code"] == "storage-unavailable" and not headers["Set-Cookies"] and command(writer, "snapshot")["active"],
        "forget reports write failure without clearing cookies or disconnecting")
    check(request(write_info, write_dir, "POST", "/api/resume", headers={"Cookie": write_cookie})[0] == 503,
        "failed pairing refresh cannot issue a fresh session")
    status, result, headers = request(write_info, write_dir, "POST", "/api/login", {"code": write_info["code"], "remember": True})
    check(status == 503 and result["code"] == "storage-unavailable" and not headers["Set-Cookies"] and len(command(writer, "snapshot")["pairings"]) == 1,
        "failed pairing enrollment does not issue a credential or mutate the device registry")
    blocked_file.rmdir(); backup_file.rename(blocked_file)
    check(command(writer, "revoke " + write_id)["revoked"], "revocation succeeds once durable storage is restored")
    finish(writer)
    expired = json.loads(json.dumps(saved))
    expired["devices"][0].update(createdAt=1, lastSeenAt=int(time.time() * 1000) - 31536000000 - 10000,
        expiresAt=int(time.time() * 1000) - 10000)
    # Use exactly one clock sample so strict timestamp validation is meaningful.
    expired["devices"][0]["expiresAt"] = expired["devices"][0]["lastSeenAt"] + 31536000000
    expiry_dir = work / "expired-pairing"; expiry_dir.mkdir()
    (expiry_dir / "pairings.json").write_text(json.dumps(expired))
    expiry, expiry_info = launch(expiry_dir)
    status, result, expiry_headers = request(expiry_info, expiry_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie})
    check("error" not in expiry_info and not command(expiry, "snapshot")["pairings"] and
        status == 401 and result["code"] == "unpaired" and not expiry_headers["Set-Cookies"],
        "expired persisted pairing cannot restore a session or mutate shared browser cookies")
    finish(expiry)
    malformed = {"truncated": '{"version":1', "oversize": " " * 32769,
        "wrong-version": json.dumps(dict(saved, version=2)), "duplicate-id": json.dumps(dict(saved, devices=saved["devices"] * 2)),
        "duplicate-hash": json.dumps(dict(saved, devices=[saved_row, dict(saved_row, id="f" * 32)])),
        "fractional-time": json.dumps(dict(saved, devices=[dict(saved_row, createdAt=1.5)])),
        "control-in-name": json.dumps(dict(saved, devices=[dict(saved_row, name="Browser\nInjected")])),
        "missing-hash": json.dumps(dict(saved, devices=[{key: value for key, value in saved_row.items() if key != "hash"}]))}
    for label, content in malformed.items():
        bad_dir = work / ("pairing-" + label); bad_dir.mkdir()
        (bad_dir / "pairings.json").write_text(content)
        bad, bad_info = launch(bad_dir)
        check("error" in bad_info and not bad_info["active"] and (bad_dir / "pairings.json").read_text() == content,
            f"{label} pairing registry fails closed without replacement")
        bad.wait(timeout=5)
    limit_dir = work / "pairing-limit"; limit_dir.mkdir()
    rows = [dict(saved_row, id=f"{i:032x}", hash=hashlib.sha256(str(i).encode()).hexdigest()) for i in range(32)]
    (limit_dir / "pairings.json").write_text(json.dumps({"version": 1, "devices": rows}))
    limit, limit_info = launch(limit_dir)
    check("error" not in limit_info and len(command(limit, "snapshot")["pairings"]) == 32, "maximum bounded pairing registry loads")
    status, result, headers = request(limit_info, limit_dir, "POST", "/api/login", {"code": limit_info["code"], "remember": True})
    check(status == 409 and result["code"] == "pairing-limit" and not headers["Set-Cookies"], "33rd device enrollment is refused without issuing cookies")
    finish(limit)
    print(f"PASS: {len(checks)} isolated HTTPS/authentication checks; build and generated test material: {work}")
finally:
    for process in processes:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
