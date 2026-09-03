from pathlib import Path


entrypoint = Path("/opt/hiclaw/scripts/worker-entrypoint.sh")
content = entrypoint.read_text(encoding="utf-8")

pull_excludes = (
    '--exclude ".openclaw/matrix/**" --exclude ".openclaw/canvas/**" '
    '--exclude "credentials/**"'
)
push_excludes = '--exclude ".cache/**" --exclude ".npm/**"'

if content.count(pull_excludes) != 2:
    raise RuntimeError("unexpected Worker entrypoint: pull exclusion block changed")
if content.count(push_excludes) != 1:
    raise RuntimeError("unexpected Worker entrypoint: push exclusion block changed")

content = content.replace(
    pull_excludes, pull_excludes + ' --exclude ".codex/tmp/**"'
)
content = content.replace(
    push_excludes, push_excludes + ' --exclude ".codex/tmp/**"'
)

entrypoint.write_text(content, encoding="utf-8")


# HiClaw's Manager/Controller may regenerate remote openclaw.json without
# applying the persisted channel-policy.json.  Because remote channels win in
# the normal merge, that silently removes Manager from a live Worker's
# allowlist.  Make the policy an invariant of every initial pull and merge.
merge_helper = Path("/opt/hiclaw/scripts/lib/merge-openclaw-config.sh")
merge_content = merge_helper.read_text(encoding="utf-8")

function_marker = "merge_openclaw_config() {\n"
if merge_content.count(function_marker) != 1:
    raise RuntimeError("unexpected merge helper: merge function marker changed")

policy_function = r'''apply_channel_policy() {
    local config_path="$1"
    local policy_path="${2:-$(dirname "${config_path}")/channel-policy.json}"
    [ -f "${config_path}" ] || return 0
    [ -f "${policy_path}" ] || return 0

    local domain policy_json tmp
    domain=$(jq -r '
        [(.channels.matrix.groupAllowFrom // [])[],
         (.channels.matrix.dm.allowFrom // [])[]]
        | map(select(type == "string" and startswith("@") and contains(":")))
        | .[0] // empty
        | sub("^@[^:]+:"; "")
    ' "${config_path}" 2>/dev/null)
    [ -n "${domain}" ] || return 0
    policy_json=$(cat "${policy_path}" 2>/dev/null) || return 1
    echo "${policy_json}" | jq -e 'type == "object"' >/dev/null 2>&1 || return 1

    tmp="${config_path}.policy-tmp"
    if jq --argjson policy "${policy_json}" --arg domain "${domain}" '
        def resolve_id: if startswith("@") then . else "@\(.):\($domain)" end;
        (if ($policy.groupAllowExtra // [] | length) > 0 then
            .channels.matrix.groupAllowFrom =
                (((.channels.matrix.groupAllowFrom // []) +
                  [$policy.groupAllowExtra[] | resolve_id]) | unique)
         else . end)
        | (if ($policy.dmAllowExtra // [] | length) > 0 then
            .channels.matrix.dm.allowFrom =
                (((.channels.matrix.dm.allowFrom // []) +
                  [$policy.dmAllowExtra[] | resolve_id]) | unique)
           else . end)
        | (if ($policy.groupDenyExtra // [] | length) > 0 then
            ([$policy.groupDenyExtra[] | resolve_id]) as $deny
            | .channels.matrix.groupAllowFrom |=
                [.[] | select(. as $id | $deny | index($id) | not)]
           else . end)
        | (if ($policy.dmDenyExtra // [] | length) > 0 then
            ([$policy.dmDenyExtra[] | resolve_id]) as $deny
            | .channels.matrix.dm.allowFrom |=
                [.[] | select(. as $id | $deny | index($id) | not)]
           else . end)
        | (if $policy.matrixStreaming != null then
            .channels.matrix.streaming = $policy.matrixStreaming
           else . end)
        | (if $policy.blockStreaming != null then
            .channels.matrix.blockStreaming = $policy.blockStreaming
           else . end)
    ' "${config_path}" > "${tmp}" 2>/dev/null; then
        mv "${tmp}" "${config_path}"
    else
        rm -f "${tmp}"
        return 1
    fi
}

'''
merge_content = merge_content.replace(function_marker, policy_function + function_marker)

old_no_remote = '''    if [ ! -f "${remote_path}" ]; then
        # No remote version, keep local as-is
        return 0
    fi
'''
new_no_remote = '''    if [ ! -f "${remote_path}" ]; then
        # No remote version, keep local as-is, but still enforce durable policy.
        apply_channel_policy "${local_path}"
        return 0
    fi
'''
if merge_content.count(old_no_remote) != 1:
    raise RuntimeError("unexpected merge helper: no-remote branch changed")
merge_content = merge_content.replace(old_no_remote, new_no_remote)

old_no_local = '''    if [ ! -f "${local_path}" ]; then
        # No local version, use remote directly
        mv "${remote_path}" "${output_path}"
        return 0
    fi
'''
new_no_local = '''    if [ ! -f "${local_path}" ]; then
        # No local version, use remote directly and then enforce durable policy.
        mv "${remote_path}" "${output_path}"
        apply_channel_policy "${output_path}"
        return 0
    fi
'''
if merge_content.count(old_no_local) != 1:
    raise RuntimeError("unexpected merge helper: no-local branch changed")
merge_content = merge_content.replace(old_no_local, new_no_local)

merge_tail = '''    else
        # jq merge failed — keep local (do not replace with remote)
        :
    fi
}
'''
merge_tail_with_policy = '''    else
        # jq merge failed — keep local (do not replace with remote)
        :
    fi
    apply_channel_policy "${output_path}"
}
'''
if merge_content.count(merge_tail) != 1:
    raise RuntimeError("unexpected merge helper: merge tail changed")
merge_content = merge_content.replace(merge_tail, merge_tail_with_policy)
merge_helper.write_text(merge_content, encoding="utf-8")

content = entrypoint.read_text(encoding="utf-8")
initial_policy_marker = "# HOME is already set to WORKSPACE via docker run -e HOME=...\n"
if content.count(initial_policy_marker) != 1:
    raise RuntimeError("unexpected Worker entrypoint: initial policy marker changed")
content = content.replace(
    initial_policy_marker,
    'apply_channel_policy "${WORKSPACE}/openclaw.json" '
    '"${WORKSPACE}/channel-policy.json"\n\n' + initial_policy_marker,
)
entrypoint.write_text(content, encoding="utf-8")
