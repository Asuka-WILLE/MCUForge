#!/usr/bin/env python3
"""Restricted Docker lifecycle bridge for MCUForge HiClaw Workers."""

from __future__ import annotations

import http.client
import json
import os
import re
import socket
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode, urlparse
from urllib.request import Request, urlopen


SOCKET_PATH = os.environ.get("DOCKER_SOCKET", "/var/run/docker.sock")
HOST = os.environ.get("WORKER_CONTROL_HOST", "0.0.0.0")
PORT = int(os.environ.get("WORKER_CONTROL_PORT", "18765"))
WORKER_RE = re.compile(r"^mcuforge-(requirements|research|firmware|verification|lead)$")
MACHINE_STATE_RE = re.compile(
    r"(?:TASK_RECEIVED|FILE_SYNC_OK|TASK_COMPLETED|SUCCESS|BLOCKED)\s*[:：]\s*([A-Za-z0-9_.:/-]+)",
    re.IGNORECASE,
)
COORD_TOKEN_RE = re.compile(r"\b(COORD_[A-Za-z0-9_.-]+)\b", re.IGNORECASE)
TASK_TOKEN_RE = re.compile(r"\b((?:task|run)[-_][A-Za-z0-9_.-]+)\b", re.IGNORECASE)
MANAGER_CONTAINER = "hiclaw-manager"
MANAGER_CONFIG = "/root/manager-workspace/openclaw.json"
COORDINATION_WORKERS = (
    "mcuforge-requirements",
    "mcuforge-research",
    "mcuforge-firmware",
    "mcuforge-verification",
)


class UnixHTTPConnection(http.client.HTTPConnection):
    def connect(self) -> None:
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(SOCKET_PATH)


def docker_request(method: str, path: str, body: dict | None = None) -> tuple[int, bytes]:
    connection = UnixHTTPConnection("localhost", timeout=30)
    try:
        payload = None if body is None else json.dumps(body, separators=(",", ":")).encode()
        headers = {} if payload is None else {"Content-Type": "application/json", "Content-Length": str(len(payload))}
        connection.request(method, path, body=payload, headers=headers)
        response = connection.getresponse()
        return response.status, response.read()
    finally:
        connection.close()


def demux_docker_stream(payload: bytes) -> bytes:
    """Decode Docker's non-TTY stdout/stderr multiplexing."""
    output = bytearray()
    offset = 0
    while offset + 8 <= len(payload):
        size = struct.unpack(">I", payload[offset + 4 : offset + 8])[0]
        frame_end = offset + 8 + size
        if frame_end > len(payload):
            break
        output.extend(payload[offset + 8 : frame_end])
        offset = frame_end
    return bytes(output) if offset else payload


def docker_exec_output(container: str, command: list[str]) -> str:
    status, payload = docker_request(
        "POST",
        f"/containers/{quote(container, safe='')}/exec",
        {
            "AttachStdout": True,
            "AttachStderr": True,
            "Tty": False,
            "Cmd": command,
        },
    )
    if status != 201:
        raise RuntimeError(f"docker_exec_create_http_{status}")
    exec_id = json.loads(payload).get("Id")
    if not exec_id:
        raise RuntimeError("docker_exec_id_missing")
    status, output = docker_request(
        "POST",
        f"/exec/{quote(exec_id, safe='')}/start",
        {"Detach": False, "Tty": False},
    )
    if status != 200:
        raise RuntimeError(f"docker_exec_start_http_{status}")
    status, payload = docker_request("GET", f"/exec/{quote(exec_id, safe='')}/json")
    if status != 200:
        raise RuntimeError(f"docker_exec_inspect_http_{status}")
    if json.loads(payload).get("ExitCode") != 0:
        raise RuntimeError("docker_exec_nonzero")
    return demux_docker_stream(output).decode("utf-8", errors="replace").strip()


def matrix_request(
    method: str,
    url: str,
    access_token: str,
    body: dict | None = None,
    timeout: int = 30,
) -> dict:
    payload = None if body is None else json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
    headers = {"Authorization": f"Bearer {access_token}"}
    if payload is not None:
        headers["Content-Type"] = "application/json"
    request = Request(url, data=payload, headers=headers, method=method)
    with urlopen(request, timeout=timeout) as response:  # noqa: S310 - fixed local HiClaw URL
        data = response.read()
    return {} if not data else json.loads(data)


def get_manager_matrix_config() -> dict:
    raw = docker_exec_output(
        MANAGER_CONTAINER,
        [
            "jq",
            "-c",
            "{homeserver:.channels.matrix.homeserver,user_id:.channels.matrix.userId,access_token:.channels.matrix.accessToken}",
            MANAGER_CONFIG,
        ],
    )
    config = json.loads(raw)
    if not all(isinstance(config.get(key), str) and config[key] for key in ("homeserver", "user_id", "access_token")):
        raise RuntimeError("manager_matrix_config_incomplete")
    return config


def extract_machine_state_token(body: str, event_id: str) -> str | None:
    if not any(marker in body.upper() for marker in (
        "TASK_RECEIVED",
        "FILE_SYNC_OK",
        "TASK_COMPLETED",
        "[PROGRESS]",
        "SUCCESS",
        "BLOCKED",
        "COORD_",
    )):
        return None
    for pattern in (MACHINE_STATE_RE, COORD_TOKEN_RE, TASK_TOKEN_RE):
        match = pattern.search(body)
        if match:
            return match.group(1)
    return event_id or "unknown"


def resolve_machine_state(body: str) -> str:
    normalized = body.upper()
    if "BLOCKED" in normalized:
        return "阻塞"
    if "TASK_COMPLETED" in normalized or "SUCCESS" in normalized:
        return "完成"
    if "[PROGRESS]" in normalized:
        return "进行中"
    return "已接收"


def container_name(worker: str) -> str:
    if not WORKER_RE.fullmatch(worker):
        raise ValueError("worker_not_allowed")
    return f"hiclaw-worker-{worker}"


def inspect_worker(worker: str) -> tuple[bool, dict]:
    name = quote(container_name(worker), safe="")
    status, payload = docker_request("GET", f"/containers/{name}/json")
    if status == 404:
        return False, {"exists": False, "worker": worker}
    if status != 200:
        raise RuntimeError(f"docker_inspect_http_{status}")
    data = json.loads(payload)
    return True, {
        "exists": True,
        "worker": worker,
        "container": data.get("Name", "").lstrip("/"),
        "id": data.get("Id", ""),
        "image": data.get("Config", {}).get("Image", ""),
        "running": bool(data.get("State", {}).get("Running", False)),
        "status": data.get("State", {}).get("Status", "unknown"),
        "started_at": data.get("State", {}).get("StartedAt", ""),
    }


def wait_running(worker: str, previous_id: str | None = None, timeout: int = 120) -> dict:
    deadline = time.monotonic() + timeout
    last: dict = {"exists": False, "worker": worker}
    while time.monotonic() < deadline:
        exists, last = inspect_worker(worker)
        id_changed = previous_id is None or last.get("id") != previous_id
        if exists and last.get("running") and id_changed:
            return last
        time.sleep(2)
    raise TimeoutError(f"worker_not_running_after_{timeout}s")


def restart_worker(worker: str) -> dict:
    exists, before = inspect_worker(worker)
    if not exists:
        raise FileNotFoundError("worker_container_not_found")
    name = quote(container_name(worker), safe="")
    status, _ = docker_request("POST", f"/containers/{name}/restart?t=20")
    if status not in (204, 304):
        raise RuntimeError(f"docker_restart_http_{status}")
    after = wait_running(worker, timeout=90)
    return {"ok": True, "action": "restarted", "before": before, "after": after}


def recreate_worker(worker: str) -> dict:
    exists, before = inspect_worker(worker)
    if not exists:
        raise FileNotFoundError("worker_container_not_found")
    name = quote(container_name(worker), safe="")
    status, _ = docker_request("DELETE", f"/containers/{name}?force=true")
    if status not in (204, 404):
        raise RuntimeError(f"docker_remove_http_{status}")
    after = wait_running(worker, previous_id=before.get("id"), timeout=120)
    return {"ok": True, "action": "recreated", "before": before, "after": after}


MANAGER_POLICY_SCRIPT = r'''set -eu
config=/root/manager-workspace/openclaw.json
remote=hiclaw/hiclaw-storage/manager/openclaw.json
[ -f "$config" ]

# Never update OpenClaw configuration with jq + mv here.  The gateway and the
# project-room lifecycle can write the same file concurrently; a stale
# read-modify-write used to erase freshly-added project-room entries.  The
# gateway config API provides an optimistic base hash and an atomic merge.
snapshot="$(openclaw gateway call config.get --json --timeout 10000)"
base_hash="$(printf '%s' "$snapshot" | jq -r '.hash // empty')"
desired="$(printf '%s' "$snapshot" | jq -c '
  (.config.channels.matrix.userId // "") as $manager
  | ($manager | split(":") | .[1:] | join(":")) as $domain
  | (["mcuforge-requirements","mcuforge-research","mcuforge-firmware","mcuforge-verification","mcuforge-lead"]
     | map("@" + . + ":" + $domain)) as $agents
  | {
      group: (((.config.channels.matrix.groupAllowFrom // []) + $agents) | unique),
      dm: (((.config.channels.matrix.dm.allowFrom // []) + $agents) | unique)
    }
')"
[ -n "$base_hash" ]

if printf '%s' "$snapshot" | jq -e --argjson desired "$desired" '
  ((.config.channels.matrix.groupAllowFrom // []) | sort) == ($desired.group | sort)
  and ((.config.channels.matrix.dm.allowFrom // []) | sort) == ($desired.dm | sort)
  and .config.channels.matrix.allowBots == "mentions"
  and .config.channels.matrix.streaming == "off"
  and .config.channels.matrix.blockStreaming == true
' >/dev/null; then
  printf unchanged
  exit 0
fi

patch="$(jq -cn --argjson desired "$desired" '
  {channels:{matrix:{
    groupAllowFrom:$desired.group,
    dm:{allowFrom:$desired.dm},
    allowBots:"mentions",
    streaming:"off",
    blockStreaming:true
  }}}
')"
params="$(jq -cn --arg raw "$patch" --arg baseHash "$base_hash" \
  '{raw:$raw,baseHash:$baseHash,note:"MCUForge Manager channel policy repaired",restartDelayMs:8000}')"
result="$(openclaw gateway call config.patch --params "$params" --json --timeout 20000)"
printf '%s' "$result" | jq -e '.ok == true' >/dev/null

# Persist the exact config accepted by the running gateway before HiClaw's
# periodic MinIO pull can restore an older copy.
mc cp "$config" "$remote" >/dev/null
mkdir -p /root/manager-workspace/.openclaw
ln -sfn "$config" /root/manager-workspace/.openclaw/openclaw.json
printf repaired'''

POLICY_LOCK = threading.Lock()
POLICY_STATE: dict = {"ok": False, "status": "not_started", "last_check": None, "last_error": None}
WORKER_POLICY_LOCK = threading.Lock()
WORKER_POLICY_STATE: dict = {
    "ok": False,
    "status": "not_started",
    "last_check": None,
    "workers": {},
    "last_error": None,
}
COORDINATOR_LOCK = threading.Lock()
COORDINATOR_STATE: dict = {
    "ok": False,
    "status": "not_started",
    "last_check": None,
    "last_ack": None,
    "last_error": None,
}


def exec_manager_policy() -> dict:
    status, payload = docker_request(
        "POST",
        "/containers/hiclaw-manager/exec",
        {
            "AttachStdout": True,
            "AttachStderr": True,
            "Tty": False,
            "Cmd": ["sh", "-lc", MANAGER_POLICY_SCRIPT],
        },
    )
    if status != 201:
        raise RuntimeError(f"manager_policy_exec_create_http_{status}")
    exec_id = json.loads(payload).get("Id")
    if not exec_id:
        raise RuntimeError("manager_policy_exec_id_missing")
    status, _ = docker_request("POST", f"/exec/{quote(exec_id, safe='')}/start", {"Detach": False, "Tty": False})
    if status != 200:
        raise RuntimeError(f"manager_policy_exec_start_http_{status}")
    status, payload = docker_request("GET", f"/exec/{quote(exec_id, safe='')}/json")
    if status != 200:
        raise RuntimeError(f"manager_policy_exec_inspect_http_{status}")
    exit_code = json.loads(payload).get("ExitCode")
    if exit_code != 0:
        raise RuntimeError(f"manager_policy_exit_{exit_code}")
    result = {"ok": True, "status": "enforced", "last_check": int(time.time()), "last_error": None}
    with POLICY_LOCK:
        POLICY_STATE.update(result)
    return result


def manager_policy_guard() -> None:
    while True:
        try:
            exec_manager_policy()
        except Exception as exc:  # noqa: BLE001
            with POLICY_LOCK:
                POLICY_STATE.update(
                    {"ok": False, "status": "error", "last_check": int(time.time()), "last_error": str(exc)}
                )
        time.sleep(3)


WORKER_POLICY_SCRIPT = r'''set -eu
worker="$1"
base="/root/hiclaw-fs/agents/${worker}"
config="${base}/openclaw.json"
policy="${base}/channel-policy.json"
remote_base="hiclaw/hiclaw-storage/agents/${worker}"
tmp="/tmp/${worker}.policy-config.json"
remote_tmp="/tmp/${worker}.remote-config.json"
policy_tmp="/tmp/${worker}.channel-policy.json"
changed=0
[ -f "$config" ]
domain=$(jq -r '.channels.matrix.userId | split(":") | .[1:] | join(":")' "$config")
[ -n "$domain" ]
manager="@manager:${domain}"
jq --arg manager "$manager" '
  .channels.matrix.groupAllowFrom = ((.channels.matrix.groupAllowFrom // []) + [$manager] | unique)
  | .channels.matrix.dm.allowFrom = ((.channels.matrix.dm.allowFrom // []) + [$manager] | unique)
  | .channels.matrix.streaming = "off"
  | .channels.matrix.blockStreaming = true
' "$config" > "$tmp"
if ! cmp -s "$config" "$tmp"; then
  mv "$tmp" "$config"
  changed=1
else
  rm -f "$tmp"
fi
printf '%s\n' '{"groupAllowExtra":["manager"],"dmAllowExtra":["manager"],"matrixStreaming":"off","blockStreaming":true}' > "$policy_tmp"
if [ ! -f "$policy" ] || ! cmp -s "$policy" "$policy_tmp"; then
  mv "$policy_tmp" "$policy"
  changed=1
else
  rm -f "$policy_tmp"
fi
mc cp "${remote_base}/openclaw.json" "$remote_tmp" >/dev/null 2>&1 || true
if [ ! -f "$remote_tmp" ] || ! cmp -s "$config" "$remote_tmp"; then
  mc cp "$config" "${remote_base}/openclaw.json" >/dev/null
  changed=1
fi
rm -f "$remote_tmp"
mc cp "${remote_base}/channel-policy.json" "$policy_tmp" >/dev/null 2>&1 || true
if [ ! -f "$policy_tmp" ] || ! cmp -s "$policy" "$policy_tmp"; then
  mc cp "$policy" "${remote_base}/channel-policy.json" >/dev/null
  changed=1
fi
rm -f "$policy_tmp"
if [ "$changed" -eq 1 ]; then printf repaired; else printf unchanged; fi'''


def exec_worker_policy(worker: str) -> str:
    return docker_exec_output(
        container_name(worker),
        ["sh", "-lc", WORKER_POLICY_SCRIPT, "mcuforge-policy", worker],
    )


def worker_policy_guard() -> None:
    while True:
        outcomes: dict[str, str] = {}
        errors: list[str] = []
        for worker in COORDINATION_WORKERS:
            try:
                outcomes[worker] = exec_worker_policy(worker) or "enforced"
            except Exception as exc:  # noqa: BLE001
                outcomes[worker] = "error"
                errors.append(f"{worker}:{type(exc).__name__}:{exc}")
        with WORKER_POLICY_LOCK:
            WORKER_POLICY_STATE.update(
                {
                    "ok": not errors,
                    "status": "enforced" if not errors else "degraded",
                    "last_check": int(time.time()),
                    "workers": outcomes,
                    "last_error": ";".join(errors) if errors else None,
                }
            )
        time.sleep(3)


def set_coordinator_state(**values: object) -> None:
    with COORDINATOR_LOCK:
        COORDINATOR_STATE.update(values)


def coordinator_receipt_relay() -> None:
    """Emit deterministic receipts before the Manager LLM finishes its turn.

    OpenClaw still receives and processes every Worker event.  This relay only
    guarantees immediate visible receipt for machine-state messages, avoiding
    model-dependent NO_REPLY decisions and heartbeat latency.
    """
    since: str | None = None
    active_identity: tuple[str, str] | None = None
    seen_event_ids: set[str] = set()

    while True:
        try:
            config = get_manager_matrix_config()
            homeserver = config["homeserver"].rstrip("/")
            manager_id = config["user_id"]
            access_token = config["access_token"]
            identity = (homeserver, manager_id)
            if identity != active_identity:
                since = None
                active_identity = identity
                seen_event_ids.clear()

            query = {"timeout": "0" if since is None else "10000"}
            if since is not None:
                query["since"] = since
            sync = matrix_request(
                "GET",
                f"{homeserver}/_matrix/client/v3/sync?{urlencode(query)}",
                access_token,
                timeout=20,
            )
            next_batch = sync.get("next_batch")
            if not isinstance(next_batch, str) or not next_batch:
                raise RuntimeError("matrix_sync_token_missing")

            # The first sync establishes a cursor and deliberately ignores old
            # room history, so a bridge restart cannot replay stale receipts.
            if since is None:
                since = next_batch
                set_coordinator_state(
                    ok=True,
                    status="watching",
                    last_check=int(time.time()),
                    last_error=None,
                )
                continue

            joined = sync.get("rooms", {}).get("join", {})
            for room_id, room_data in joined.items():
                events = room_data.get("timeline", {}).get("events", [])
                for event in events:
                    event_id = event.get("event_id", "")
                    if not event_id or event_id in seen_event_ids or event.get("type") != "m.room.message":
                        continue
                    seen_event_ids.add(event_id)
                    if len(seen_event_ids) > 2000:
                        seen_event_ids.clear()
                        seen_event_ids.add(event_id)
                    sender = event.get("sender", "")
                    if not WORKER_RE.fullmatch(sender.split(":", 1)[0].lstrip("@")):
                        continue
                    content = event.get("content", {})
                    if content.get("m.relates_to", {}).get("rel_type") == "m.replace":
                        continue
                    mentions = content.get("m.mentions", {}).get("user_ids", [])
                    if manager_id not in mentions:
                        continue
                    body = content.get("body", "")
                    if not isinstance(body, str):
                        continue
                    task_id = extract_machine_state_token(body, event_id)
                    if task_id is None:
                        continue
                    machine_state = resolve_machine_state(body)
                    receipt = (
                        f"[COORDINATOR_ACK] task_id={task_id} "
                        f"state={machine_state} next=Manager正在处理"
                    )
                    transaction_id = f"mcuforge-receipt-{int(time.time() * 1000)}-{abs(hash(event_id))}"
                    matrix_request(
                        "PUT",
                        f"{homeserver}/_matrix/client/v3/rooms/{quote(room_id, safe='')}/send/m.room.message/{quote(transaction_id, safe='')}",
                        access_token,
                        {"msgtype": "m.text", "body": receipt, "m.mentions": {"user_ids": []}},
                        timeout=15,
                    )
                    set_coordinator_state(
                        ok=True,
                        status="watching",
                        last_check=int(time.time()),
                        last_ack={"room_id": room_id, "event_id": event_id, "task_id": task_id},
                        last_error=None,
                    )

            since = next_batch
            set_coordinator_state(
                ok=True,
                status="watching",
                last_check=int(time.time()),
                last_error=None,
            )
        except (HTTPError, URLError, TimeoutError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
            set_coordinator_state(
                ok=False,
                status="error",
                last_check=int(time.time()),
                last_error=f"{type(exc).__name__}:{exc}",
            )
            # Keep the last successful Matrix cursor.  Resetting it after a
            # transient Docker/Matrix error would make the next initial sync
            # skip Worker events that arrived during the outage.
            time.sleep(3)


class Handler(BaseHTTPRequestHandler):
    server_version = "MCUForgeWorkerControl/1.0"

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"[{self.log_date_time_string()}] {fmt % args}", flush=True)

    def send_json(self, status: int, body: dict) -> None:
        payload = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def parse_worker_action(self) -> tuple[str, str] | None:
        match = re.fullmatch(r"/v1/workers/([^/]+)(?:/(restart|recreate))?", urlparse(self.path).path)
        if not match:
            return None
        return match.group(1), match.group(2) or "inspect"

    def do_GET(self) -> None:  # noqa: N802
        if urlparse(self.path).path == "/health":
            try:
                status, _ = docker_request("GET", "/_ping")
                with POLICY_LOCK:
                    policy = dict(POLICY_STATE)
                with WORKER_POLICY_LOCK:
                    worker_policy = dict(WORKER_POLICY_STATE)
                with COORDINATOR_LOCK:
                    coordinator = dict(COORDINATOR_STATE)
                ok = status == 200 and policy.get("ok") is True and coordinator.get("ok") is True
                self.send_json(
                    200 if ok else 503,
                    {
                        "ok": ok,
                        "manager_policy": policy,
                        "worker_policy": worker_policy,
                        "coordinator_relay": coordinator,
                    },
                )
            except Exception as exc:  # noqa: BLE001
                self.send_json(503, {"ok": False, "error": str(exc)})
            return
        parsed = self.parse_worker_action()
        if not parsed or parsed[1] != "inspect":
            self.send_json(404, {"ok": False, "error": "not_found"})
            return
        try:
            _, data = inspect_worker(parsed[0])
            self.send_json(200, {"ok": True, **data})
        except ValueError as exc:
            self.send_json(403, {"ok": False, "error": str(exc)})
        except Exception as exc:  # noqa: BLE001
            self.send_json(500, {"ok": False, "error": str(exc)})

    def do_POST(self) -> None:  # noqa: N802
        if urlparse(self.path).path == "/v1/manager-policy/reconcile":
            try:
                self.send_json(200, exec_manager_policy())
            except Exception as exc:  # noqa: BLE001
                self.send_json(500, {"ok": False, "error": str(exc)})
            return
        parsed = self.parse_worker_action()
        if not parsed or parsed[1] not in {"restart", "recreate"}:
            self.send_json(404, {"ok": False, "error": "not_found"})
            return
        worker, action = parsed
        try:
            result = restart_worker(worker) if action == "restart" else recreate_worker(worker)
            self.send_json(200, result)
        except ValueError as exc:
            self.send_json(403, {"ok": False, "error": str(exc)})
        except FileNotFoundError as exc:
            self.send_json(404, {"ok": False, "error": str(exc)})
        except TimeoutError as exc:
            self.send_json(504, {"ok": False, "error": str(exc)})
        except Exception as exc:  # noqa: BLE001
            self.send_json(500, {"ok": False, "error": str(exc)})


if __name__ == "__main__":
    threading.Thread(target=manager_policy_guard, name="manager-policy-guard", daemon=True).start()
    threading.Thread(target=worker_policy_guard, name="worker-policy-guard", daemon=True).start()
    threading.Thread(target=coordinator_receipt_relay, name="coordinator-receipt-relay", daemon=True).start()
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"worker-control listening on {HOST}:{PORT}", flush=True)
    server.serve_forever()
