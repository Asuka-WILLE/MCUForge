#!/usr/bin/env bash
# MCUForge auditable, idempotent Matrix project-room creator.

set -Eeuo pipefail
source /opt/hiclaw/scripts/lib/hiclaw-env.sh

PROJECT_ID=""
PROJECT_TITLE=""
WORKERS_CSV="mcuforge-lead,mcuforge-requirements,mcuforge-research,mcuforge-firmware,mcuforge-verification"
SOURCE_ROOM_ID=""
SOURCE_EVENT_ID=""
ORIGINAL_REQUEST=""
ORIGINAL_EVENT_ID=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --id) PROJECT_ID="${2:-}"; shift 2 ;;
        --title) PROJECT_TITLE="${2:-}"; shift 2 ;;
        --workers) WORKERS_CSV="${2:-}"; shift 2 ;;
        --source-room) SOURCE_ROOM_ID="${2:-}"; shift 2 ;;
        --source-event) SOURCE_EVENT_ID="${2:-}"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

if [[ ! "${PROJECT_ID}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{2,79}$ ]] || [ -z "${PROJECT_TITLE}" ]; then
    echo 'Usage: create-project.sh --id <PROJECT_ID> --title <TITLE> [--workers <csv>] [--source-room <id>] [--source-event <id>]' >&2
    exit 2
fi

MATRIX_DOMAIN="${HICLAW_MATRIX_DOMAIN:-matrix-local.hiclaw.io:8080}"
ADMIN_USER="${HICLAW_ADMIN_USER:-admin}"
MATRIX_URL="${HICLAW_MATRIX_URL:?HICLAW_MATRIX_URL is required}"
STORAGE_PREFIX="${HICLAW_STORAGE_PREFIX:-hiclaw/hiclaw-storage}"
PROJECT_DIR="/root/hiclaw-fs/shared/projects/${PROJECT_ID}"
META_PATH="${PROJECT_DIR}/meta.json"
AUDIT_PATH="${PROJECT_DIR}/audit.ndjson"
PLAN_PATH="${PROJECT_DIR}/plan.md"
NOW="$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
ALIAS_LOCAL="$(printf '%s' "${PROJECT_ID}" | tr '[:upper:]' '[:lower:]' | tr -c 'a-z0-9._=-' '-')"
ROOM_ALIAS="#${ALIAS_LOCAL}:${MATRIX_DOMAIN}"
MANAGER_ID="@manager:${MATRIX_DOMAIN}"
ADMIN_ID="@${ADMIN_USER}:${MATRIX_DOMAIN}"
LAST_STAGE="CREATING"
LAST_ERROR="unexpected_error"

mkdir -p "${PROJECT_DIR}"

audit_event() {
    local state="$1"
    local detail="$2"
    jq -cn --arg ts "$(date -u +'%Y-%m-%dT%H:%M:%SZ')" --arg state "${state}" \
        --arg detail "${detail}" --arg project_id "${PROJECT_ID}" \
        '{ts:$ts,project_id:$project_id,state:$state,detail:$detail}' >> "${AUDIT_PATH}"
}

sync_project() {
    mc mirror "${PROJECT_DIR}/" "${STORAGE_PREFIX}/shared/projects/${PROJECT_ID}/" --overwrite >/dev/null
    mc stat "${STORAGE_PREFIX}/shared/projects/${PROJECT_ID}/meta.json" >/dev/null
}

mark_blocked() {
    local exit_code=$?
    trap - ERR
    set +e
    if [ -f "${META_PATH}" ]; then
        jq --arg state "BLOCKED" --arg error "${LAST_STAGE}: ${LAST_ERROR}" \
            '.lifecycle_state=$state | .last_error=$error | .updated_at=(now|todateiso8601)' \
            "${META_PATH}" > "/tmp/${PROJECT_ID}-blocked-$$.json" && \
            mv "/tmp/${PROJECT_ID}-blocked-$$.json" "${META_PATH}"
        audit_event "BLOCKED" "${LAST_STAGE}: ${LAST_ERROR}"
        sync_project >/dev/null 2>&1
    fi
    jq -cn --arg error "${LAST_ERROR}" --arg stage "${LAST_STAGE}" --arg project_id "${PROJECT_ID}" \
        '{error:$error,stage:$stage,project_id:$project_id}' >&2
    exit "${exit_code}"
}
trap mark_blocked ERR

WORKERS_JSON="$(jq -cn --arg csv "${WORKERS_CSV}" '$csv|split(",")|map(gsub("^\\s+|\\s+$";""))|map(select(length>0))|unique')"
ALLOWED_JSON='["mcuforge-lead","mcuforge-requirements","mcuforge-research","mcuforge-firmware","mcuforge-verification"]'
INVALID_WORKERS="$(jq -rnc --argjson requested "${WORKERS_JSON}" --argjson allowed "${ALLOWED_JSON}" '$requested-$allowed|join(",")')"
if [ -n "${INVALID_WORKERS}" ]; then
    LAST_ERROR="worker_not_allowed:${INVALID_WORKERS}"
    false
fi

if [ -f "/data/hiclaw-secrets.env" ]; then
    # shellcheck disable=SC1091
    source /data/hiclaw-secrets.env
fi
RUNTIME_MANAGER_TOKEN=""
for runtime_config in "${HOME}/openclaw.json" "${HOME}/.openclaw/openclaw.json"; do
    if [ -f "${runtime_config}" ]; then
        RUNTIME_MANAGER_TOKEN="$(jq -r '.channels.matrix.accessToken // empty' "${runtime_config}")"
        [ -n "${RUNTIME_MANAGER_TOKEN}" ] && break
    fi
done
if [ -n "${RUNTIME_MANAGER_TOKEN}" ]; then
    MANAGER_MATRIX_TOKEN="${RUNTIME_MANAGER_TOKEN}"
elif [ -z "${MANAGER_MATRIX_TOKEN:-}" ]; then
    MANAGER_MATRIX_TOKEN="$(curl -fsS -X POST "${MATRIX_URL}/_matrix/client/v3/login" \
        -H 'Content-Type: application/json' \
        -d "$(jq -cn --arg user manager --arg password "${HICLAW_MANAGER_PASSWORD:?manager password missing}" \
            '{type:"m.login.password",identifier:{type:"m.id.user",user:$user},password:$password}')" | jq -r '.access_token // empty')"
fi
if [ -z "${MANAGER_MATRIX_TOKEN:-}" ]; then
    LAST_ERROR="manager_token_unavailable"
    false
fi
ADMIN_LOGIN="$(curl -fsS -X POST "${MATRIX_URL}/_matrix/client/v3/login" \
    -H 'Content-Type: application/json' \
    -d "$(jq -cn --arg user "${ADMIN_USER}" --arg password "${HICLAW_ADMIN_PASSWORD:?admin password missing}" \
        '{type:"m.login.password",identifier:{type:"m.id.user",user:$user},password:$password}')")"
ADMIN_MATRIX_TOKEN="$(printf '%s' "${ADMIN_LOGIN}" | jq -r '.access_token // empty')"
if [ -z "${ADMIN_MATRIX_TOKEN}" ]; then
    LAST_ERROR="admin_token_unavailable"
    false
fi
ADMIN_ID="$(printf '%s' "${ADMIN_LOGIN}" | jq -r --arg fallback "${ADMIN_ID}" '.user_id // $fallback')"
PARTICIPANTS_JSON="$(jq -cn --arg manager "${MANAGER_ID}" --arg admin "${ADMIN_ID}" \
    --arg domain "${MATRIX_DOMAIN}" --argjson workers "${WORKERS_JSON}" \
    '[$manager,$admin]+($workers|map("@"+.+":"+$domain))|unique')"

IS_NEW_PROJECT=false
[ -f "${META_PATH}" ] || IS_NEW_PROJECT=true
if [ "${IS_NEW_PROJECT}" = false ]; then
    # A retry must continue from the exact original event recorded by the first
    # attempt.  Falling back to the newest DM message can silently replace the
    # request or leave READY metadata without an original-request event.
    if [ -z "${SOURCE_ROOM_ID}" ]; then
        SOURCE_ROOM_ID="$(jq -r '.source_room_id // empty' "${META_PATH}")"
    fi
    if [ -z "${SOURCE_EVENT_ID}" ]; then
        SOURCE_EVENT_ID="$(jq -r '.source_event_id // empty' "${META_PATH}")"
    fi
fi
if [ -z "${SOURCE_ROOM_ID}" ] && [ -f "${HOME}/state.json" ]; then
    SOURCE_ROOM_ID="$(jq -r '.admin_dm_room_id // empty' "${HOME}/state.json")"
fi
if [ -n "${SOURCE_ROOM_ID}" ]; then
    SOURCE_ROOM_ENC="$(jq -rn --arg value "${SOURCE_ROOM_ID}" '$value|@uri')"
    SOURCE_MESSAGES="$(curl -fsS "${MATRIX_URL}/_matrix/client/v3/rooms/${SOURCE_ROOM_ENC}/messages?dir=b&limit=50" \
        -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}")"
    if [ -n "${SOURCE_EVENT_ID}" ]; then
        SOURCE_EVENT_JSON="$(printf '%s' "${SOURCE_MESSAGES}" | jq -c --arg event_id "${SOURCE_EVENT_ID}" \
            '[.chunk[]|select(.event_id==$event_id and .type=="m.room.message")]|first // empty')"
    else
        SOURCE_EVENT_JSON="$(printf '%s' "${SOURCE_MESSAGES}" | jq -c --arg admin "${ADMIN_ID}" \
            '[.chunk[]|select((.sender|ascii_downcase)==($admin|ascii_downcase) and .type=="m.room.message" and (.content.body|type)=="string")]
             | sort_by(.origin_server_ts) | last // empty')"
    fi
    if [ -n "${SOURCE_EVENT_JSON}" ]; then
        ORIGINAL_REQUEST="$(printf '%s' "${SOURCE_EVENT_JSON}" | jq -r '.content.body')"
        ORIGINAL_EVENT_ID="$(printf '%s' "${SOURCE_EVENT_JSON}" | jq -r '.event_id // empty')"
        SOURCE_EVENT_TS="$(printf '%s' "${SOURCE_EVENT_JSON}" | jq -r '.origin_server_ts // 0')"
        CURRENT_TS_MS="$(( $(date +%s) * 1000 ))"
        if [ "${IS_NEW_PROJECT}" = true ] && [ $((CURRENT_TS_MS - SOURCE_EVENT_TS)) -gt 600000 ]; then
            LAST_ERROR="source_request_older_than_10_minutes"
            false
        fi
    fi
fi
if [ "${IS_NEW_PROJECT}" = true ] && [ -z "${ORIGINAL_REQUEST}" ]; then
    LAST_ERROR="original_request_not_found"
    false
fi

if [ -f "${META_PATH}" ]; then
    EXISTING_TITLE="$(jq -r '.title // empty' "${META_PATH}")"
    EXISTING_SCHEMA="$(jq -r '.schema_version // 1' "${META_PATH}")"
    EXISTING_MODE="$(jq -r '.interaction_mode // empty' "${META_PATH}")"
    if [ -n "${EXISTING_TITLE}" ] && [ "${EXISTING_TITLE}" != "${PROJECT_TITLE}" ]; then
        LAST_ERROR="project_id_title_conflict"
        false
    fi
    jq --argjson workers "${WORKERS_JSON}" --argjson participants "${PARTICIPANTS_JSON}" \
        --arg alias "${ROOM_ALIAS}" --arg source_room "${SOURCE_ROOM_ID}" --arg source_event "${ORIGINAL_EVENT_ID}" \
        '.schema_version=2 | .interaction_mode="project_room_only" | .workers=$workers
         | .participants=$participants | .room_alias=(.room_alias//$alias)
         | .source_room_id=(.source_room_id//($source_room|if length>0 then . else null end))
         | .source_event_id=(.source_event_id//($source_event|if length>0 then . else null end))
         | .room_ready_at=(.room_ready_at//null) | .room_initialized_at=(.room_initialized_at//null)
         | .last_error=(.last_error//null) | .updated_at=(now|todateiso8601)' \
        "${META_PATH}" > "/tmp/${PROJECT_ID}-participants-$$.json"
    mv "/tmp/${PROJECT_ID}-participants-$$.json" "${META_PATH}"
    if [ "${EXISTING_SCHEMA}" != "2" ] || [ "${EXISTING_MODE}" != "project_room_only" ]; then
        audit_event "MIGRATED" "metadata upgraded to schema_version=2 and project_room_only"
    fi
else
    jq -n --arg id "${PROJECT_ID}" --arg title "${PROJECT_TITLE}" --arg alias "${ROOM_ALIAS}" \
        --arg source_room "${SOURCE_ROOM_ID}" --arg source_event "${ORIGINAL_EVENT_ID}" \
        --arg now "${NOW}" --argjson workers "${WORKERS_JSON}" --argjson participants "${PARTICIPANTS_JSON}" \
        '{schema_version:2,project_id:$id,title:$title,project_room_id:null,room_alias:$alias,
          interaction_mode:"project_room_only",lifecycle_state:"CREATING",status:"planning",
          workers:$workers,participants:$participants,
          source_room_id:($source_room|if length>0 then . else null end),
          source_event_id:($source_event|if length>0 then . else null end),created_at:$now,updated_at:$now,
          room_ready_at:null,room_initialized_at:null,confirmed_at:null,last_error:null}' > "${META_PATH}"
    audit_event "CREATING" "project metadata initialized"
fi

if [ -n "${ORIGINAL_REQUEST}" ]; then
    ORIGINAL_SHA="$(printf '%s' "${ORIGINAL_REQUEST}" | sha256sum | cut -d ' ' -f1)"
    printf '# Original Request\n\n- Source room: %s\n- Source event: %s\n- SHA-256: %s\n\n%s\n' \
        "${SOURCE_ROOM_ID}" "${ORIGINAL_EVENT_ID}" "${ORIGINAL_SHA}" "${ORIGINAL_REQUEST}" > "${PROJECT_DIR}/request.md"
    jq --arg source_room "${SOURCE_ROOM_ID}" --arg source_event "${ORIGINAL_EVENT_ID}" --arg sha "${ORIGINAL_SHA}" \
        '.source_room_id=$source_room | .source_event_id=$source_event | .original_request_sha256=$sha' \
        "${META_PATH}" > "/tmp/${PROJECT_ID}-request-$$.json"
    mv "/tmp/${PROJECT_ID}-request-$$.json" "${META_PATH}"
fi

if [ ! -f "${PLAN_PATH}" ]; then
    cat > "${PLAN_PATH}" <<EOF
# Project: ${PROJECT_TITLE}

**ID**: ${PROJECT_ID}
**Status**: planning
**Interaction**: project_room_only
**Created**: ${NOW}

## Original Request

(Manager must copy the original human request here and into the project room.)

## Task Plan

(To be drafted and confirmed in the project room.)

## Change Log

- ${NOW}: Project room lifecycle started.
EOF
fi

ROOM_ID="$(jq -r '.project_room_id // empty' "${META_PATH}")"
if [ -z "${ROOM_ID}" ]; then
    LAST_STAGE="ROOM_CREATED"
    # The local Matrix server does not allow its special human-admin account to
    # accept an invite to a private room.  Create as admin instead, so the human
    # is joined from the first event; Manager and Workers then accept invites.
    INVITES_JSON="$(jq -cn --argjson participants "${PARTICIPANTS_JSON}" --arg admin "${ADMIN_ID}" '$participants|map(select(.!=$admin))')"
    POWER_USERS="$(jq -cn --arg manager "${MANAGER_ID}" --arg admin "${ADMIN_ID}" --argjson participants "${PARTICIPANTS_JSON}" \
        'reduce $participants[] as $id ({($manager):100,($admin):100}; if ($id==$manager or $id==$admin) then . else .[$id]=0 end)')"
    CREATE_PAYLOAD="$(jq -cn --arg alias "${ALIAS_LOCAL}" --arg name "Project: ${PROJECT_TITLE}" \
        --arg topic "MCUForge auditable project ${PROJECT_ID}; all project communication stays in this room" \
        --argjson invite "${INVITES_JSON}" --argjson users "${POWER_USERS}" \
        '{room_alias_name:$alias,name:$name,topic:$topic,invite:$invite,preset:"trusted_private_chat",power_level_content_override:{users:$users}}')"
    ROOM_RESP="$(curl -sS -X POST "${MATRIX_URL}/_matrix/client/v3/createRoom" \
        -H "Authorization: Bearer ${ADMIN_MATRIX_TOKEN}" -H 'Content-Type: application/json' -d "${CREATE_PAYLOAD}")"
    ROOM_ID="$(printf '%s' "${ROOM_RESP}" | jq -r '.room_id // empty')"
    if [ -z "${ROOM_ID}" ]; then
        ALIAS_ENC="$(jq -rn --arg value "${ROOM_ALIAS}" '$value|@uri')"
        ROOM_ID="$(curl -fsS "${MATRIX_URL}/_matrix/client/v3/directory/room/${ALIAS_ENC}" \
            -H "Authorization: Bearer ${ADMIN_MATRIX_TOKEN}" | jq -r '.room_id // empty')"
    fi
    if [ -z "${ROOM_ID}" ]; then
        LAST_ERROR="matrix_room_create_failed"
        false
    fi
    jq --arg room "${ROOM_ID}" '.project_room_id=$room | .lifecycle_state="ROOM_CREATED" | .updated_at=(now|todateiso8601)' \
        "${META_PATH}" > "/tmp/${PROJECT_ID}-room-$$.json"
    mv "/tmp/${PROJECT_ID}-room-$$.json" "${META_PATH}"
    audit_event "ROOM_CREATED" "room_id=${ROOM_ID}"
else
    audit_event "ROOM_REUSED" "room_id=${ROOM_ID}"
fi

ALIAS_ENC="$(jq -rn --arg value "${ROOM_ALIAS}" '$value|@uri')"
curl -sS -X PUT "${MATRIX_URL}/_matrix/client/v3/directory/room/${ALIAS_ENC}" \
    -H "Authorization: Bearer ${ADMIN_MATRIX_TOKEN}" -H 'Content-Type: application/json' \
    -d "$(jq -cn --arg room_id "${ROOM_ID}" '{room_id:$room_id}')" >/dev/null || true

identity_token() {
    local user="$1"
    local password=""
    local creds_file="/data/worker-creds/${user}.env"
    if [ "${user}" = "manager" ]; then
        printf '%s' "${MANAGER_MATRIX_TOKEN}"
        return 0
    elif [ "${user}" = "${ADMIN_USER}" ]; then
        printf '%s' "${ADMIN_MATRIX_TOKEN}"
        return 0
    else
        if [ -f "${creds_file}" ]; then
            password="$(WORKER_PASSWORD=''; source "${creds_file}"; printf '%s' "${WORKER_PASSWORD:-}")"
        fi
        if [ -z "${password}" ]; then
            password="$(mc cat "${STORAGE_PREFIX}/agents/${user}/credentials/matrix/password" 2>/dev/null || true)"
        fi
        if [ -z "${password}" ] && [ -f "/root/hiclaw-fs/agents/${user}/credentials/matrix/password" ]; then
            password="$(cat "/root/hiclaw-fs/agents/${user}/credentials/matrix/password")"
        fi
    fi
    if [ -n "${password}" ]; then
        curl -fsS -X POST "${MATRIX_URL}/_matrix/client/v3/login" -H 'Content-Type: application/json' \
            -d "$(jq -cn --arg user "${user}" --arg password "${password}" \
                '{type:"m.login.password",identifier:{type:"m.id.user",user:$user},password:$password}')" | jq -r '.access_token // empty'
    fi
}

member_is_joined() {
    local user_id="$1"
    curl -fsS "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/joined_members" \
        -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" | \
        jq -e --arg user_id "${user_id}" '.joined[$user_id] != null' >/dev/null
}

wait_for_member() {
    local user_id="$1"
    local timeout_seconds="${2:-20}"
    local deadline=$((SECONDS + timeout_seconds))
    while [ "${SECONDS}" -lt "${deadline}" ]; do
        if member_is_joined "${user_id}"; then
            return 0
        fi
        sleep 1
    done
    member_is_joined "${user_id}"
}

LAST_STAGE="MEMBERS_READY"
ROOM_ENC="$(jq -rn --arg value "${ROOM_ID}" '$value|@uri')"
# Manager must join first so it can own subsequent orchestration and recovery.
if ! curl -fsS -X POST "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/join" \
    -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" -H 'Content-Type: application/json' -d '{}' >/dev/null; then
    LAST_ERROR="member_join_failed:manager"
    false
fi
for user in "${ADMIN_USER}" $(printf '%s' "${WORKERS_JSON}" | jq -r '.[]'); do
    if [ "${user}" = "${ADMIN_USER}" ]; then
        user_id="${ADMIN_ID}"
    else
        user_id="@${user}:${MATRIX_DOMAIN}"
    fi
    if member_is_joined "${user_id}"; then
        continue
    fi
    curl -sS -X POST "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/invite" \
        -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" -H 'Content-Type: application/json' \
        -d "$(jq -cn --arg user_id "${user_id}" '{user_id:$user_id}')" >/dev/null || true
    token="$(identity_token "${user}")"
    if [ -z "${token}" ]; then
        # Worker invitations are also consumed by the worker-side autoJoin
        # loop.  Credentials can be briefly unavailable while that loop is
        # starting, so observe the actual room membership before declaring a
        # failure.  This avoids a false BLOCKED followed by an unsafe manual
        # recovery path.
        if wait_for_member "${user_id}" 20; then
            audit_event "MEMBER_AUTOJOINED" "user=${user_id}"
            continue
        fi
        LAST_ERROR="matrix_token_unavailable_and_not_joined:${user}"
        false
    fi
    if ! curl -fsS -X POST "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/join" \
        -H "Authorization: Bearer ${token}" -H 'Content-Type: application/json' -d '{}' >/dev/null; then
        if [ "${user}" = "${ADMIN_USER}" ]; then
            # Compatibility recovery for rooms created by the legacy Manager-
            # creator flow. Keep the public window bounded to this join call.
            curl -fsS -X PUT "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/state/m.room.join_rules" \
                -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" -H 'Content-Type: application/json' \
                -d '{"join_rule":"public"}' >/dev/null
            if ! curl -fsS -X POST "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/join" \
                -H "Authorization: Bearer ${token}" -H 'Content-Type: application/json' -d '{}' >/dev/null; then
                curl -sS -X PUT "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/state/m.room.join_rules" \
                    -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" -H 'Content-Type: application/json' \
                    -d '{"join_rule":"invite"}' >/dev/null || true
                LAST_ERROR="member_join_failed:${user}"
                false
            fi
            curl -fsS -X PUT "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/state/m.room.join_rules" \
                -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" -H 'Content-Type: application/json' \
                -d '{"join_rule":"invite"}' >/dev/null
        else
            LAST_ERROR="member_join_failed:${user}"
            false
        fi
    fi
done

MEMBER_DEADLINE=$((SECONDS + 15))
MISSING_JSON="${PARTICIPANTS_JSON}"
while [ "${SECONDS}" -lt "${MEMBER_DEADLINE}" ]; do
    JOINED_JSON="$(curl -fsS "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/joined_members" \
        -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}")"
    MISSING_JSON="$(jq -cn --argjson expected "${PARTICIPANTS_JSON}" \
        --argjson joined "$(printf '%s' "${JOINED_JSON}" | jq '.joined|keys')" \
        '($expected|map(ascii_downcase))-($joined|map(ascii_downcase))')"
    if [ "$(printf '%s' "${MISSING_JSON}" | jq 'length')" -eq 0 ]; then
        break
    fi
    sleep 1
done
if [ "$(printf '%s' "${MISSING_JSON}" | jq 'length')" -ne 0 ]; then
    audit_event "MEMBERS_SNAPSHOT" "joined=$(printf '%s' "${JOINED_JSON}" | jq -r '.joined|keys|join(",")')"
    LAST_ERROR="members_not_joined:$(printf '%s' "${MISSING_JSON}" | jq -r 'join(",")')"
    false
fi
audit_event "MEMBERS_READY" "all participants joined"

ORIGINAL_REQUEST_EVENT_ID="$(jq -r '.original_request_event_id // empty' "${META_PATH}")"
if [ -n "${ORIGINAL_REQUEST}" ] && [ -z "${ORIGINAL_REQUEST_EVENT_ID}" ]; then
    ORIGINAL_TXN="mcuforge-original-${PROJECT_ID}-$(date +%s%N)"
    ORIGINAL_BODY="[ORIGINAL_REQUEST] project_id=${PROJECT_ID}\n${ORIGINAL_REQUEST}"
    ORIGINAL_RESPONSE="$(curl -fsS -X PUT "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/send/m.room.message/${ORIGINAL_TXN}" \
        -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" -H 'Content-Type: application/json' \
        -d "$(jq -cn --arg body "${ORIGINAL_BODY}" --arg admin "${ADMIN_ID}" \
            '{msgtype:"m.text",body:$body,"m.mentions":{user_ids:[$admin]}}')")"
    ORIGINAL_REQUEST_EVENT_ID="$(printf '%s' "${ORIGINAL_RESPONSE}" | jq -r '.event_id // empty')"
    if [ -z "${ORIGINAL_REQUEST_EVENT_ID}" ]; then
        LAST_ERROR="original_request_event_missing"
        false
    fi
    jq --arg event_id "${ORIGINAL_REQUEST_EVENT_ID}" --arg recorded_at "$(date -u +'%Y-%m-%dT%H:%M:%SZ')" \
        '.original_request_event_id=$event_id | .original_request_recorded_at=$recorded_at' \
        "${META_PATH}" > "/tmp/${PROJECT_ID}-request-event-$$.json"
    mv "/tmp/${PROJECT_ID}-request-event-$$.json" "${META_PATH}"
    audit_event "ORIGINAL_REQUEST_RECORDED" "event_id=${ORIGINAL_REQUEST_EVENT_ID}"
fi

# The per-room Matrix config is hot-reloaded below.  Do not rely on the model
# to send the DM migration notice during that restart window; emit one
# deterministic, idempotent event first and persist its event id.
SOURCE_NOTICE_EVENT_ID="$(jq -r '.source_notice_event_id // empty' "${META_PATH}")"
if [ -n "${SOURCE_ROOM_ID}" ] && [ -z "${SOURCE_NOTICE_EVENT_ID}" ]; then
    SOURCE_ROOM_ENC="$(jq -rn --arg value "${SOURCE_ROOM_ID}" '$value|@uri')"
    SOURCE_NOTICE_TXN="mcuforge-moved-${PROJECT_ID}-$(date +%s%N)"
    SOURCE_NOTICE_BODY="[PROJECT_MOVED] project_id=${PROJECT_ID} room_id=${ROOM_ID}。项目《${PROJECT_TITLE}》已迁移到独立房间；后续需求、确认、派工、进度、异常与结项只在该房间进行。"
    SOURCE_NOTICE_RESPONSE="$(curl -fsS -X PUT \
        "${MATRIX_URL}/_matrix/client/v3/rooms/${SOURCE_ROOM_ENC}/send/m.room.message/${SOURCE_NOTICE_TXN}" \
        -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" -H 'Content-Type: application/json' \
        -d "$(jq -cn --arg body "${SOURCE_NOTICE_BODY}" --arg admin "${ADMIN_ID}" \
            '{msgtype:"m.text",body:$body,"m.mentions":{user_ids:[$admin]}}')")"
    SOURCE_NOTICE_EVENT_ID="$(printf '%s' "${SOURCE_NOTICE_RESPONSE}" | jq -r '.event_id // empty')"
    if [ -z "${SOURCE_NOTICE_EVENT_ID}" ]; then
        LAST_ERROR="source_notice_event_missing"
        false
    fi
    jq --arg event_id "${SOURCE_NOTICE_EVENT_ID}" --arg recorded_at "$(date -u +'%Y-%m-%dT%H:%M:%SZ')" \
        '.source_notice_event_id=$event_id | .source_notice_recorded_at=$recorded_at' \
        "${META_PATH}" > "/tmp/${PROJECT_ID}-source-notice-$$.json"
    mv "/tmp/${PROJECT_ID}-source-notice-$$.json" "${META_PATH}"
    audit_event "SOURCE_NOTICE_RECORDED" "event_id=${SOURCE_NOTICE_EVENT_ID}"
fi

LAST_STAGE="MANAGER_ROOM_CONFIG"
LAST_ERROR="manager_gateway_config_unavailable"
CONFIG_SNAPSHOT="$(openclaw gateway call config.get --json --timeout 10000)"
CONFIG_HASH="$(printf '%s' "${CONFIG_SNAPSHOT}" | jq -r '.hash // empty')"
CURRENT_GROUP_ALLOW="$(printf '%s' "${CONFIG_SNAPSHOT}" | jq -c '.config.channels.matrix.groupAllowFrom // []')"
if [ -z "${CONFIG_HASH}" ] || [ -z "${CURRENT_GROUP_ALLOW}" ]; then
    false
fi

CONFIG_PATCH="$(jq -cn --arg room "${ROOM_ID}" \
    --argjson current "${CURRENT_GROUP_ALLOW}" --argjson participants "${PARTICIPANTS_JSON}" \
    '{channels:{matrix:{groupAllowFrom:(($current+$participants)|unique),
      groups:{($room):{enabled:true,requireMention:false,autoReply:true}}}}}')"
CONFIG_PARAMS="$(jq -cn --arg raw "${CONFIG_PATCH}" --arg baseHash "${CONFIG_HASH}" \
    --arg note "MCUForge project room ${PROJECT_ID} is ready" \
    '{raw:$raw,baseHash:$baseHash,note:$note,restartDelayMs:8000}')"
CONFIG_RESULT="$(openclaw gateway call config.patch --params "${CONFIG_PARAMS}" --json --timeout 20000)"
if ! printf '%s' "${CONFIG_RESULT}" | jq -e '.ok == true' >/dev/null; then
    LAST_ERROR="manager_gateway_config_patch_failed"
    false
fi

# HiClaw periodically pulls this file from MinIO.  Persist the just-validated
# gateway config immediately, before an older remote copy can overwrite it.
mc cp "${HOME}/openclaw.json" "${STORAGE_PREFIX}/manager/openclaw.json" >/dev/null

# Older MCUForge revisions replaced this symlink with a second regular file.
# Restore one active config path; never maintain two divergent copies.
mkdir -p "${HOME}/.openclaw"
ln -sfn "${HOME}/openclaw.json" "${HOME}/.openclaw/openclaw.json"

if ! jq -e --arg room "${ROOM_ID}" \
    '.channels.matrix.groups[$room].enabled == true
     and .channels.matrix.groups[$room].requireMention == false
     and .channels.matrix.groups[$room].autoReply == true' \
    "${HOME}/openclaw.json" >/dev/null; then
    LAST_ERROR="manager_room_config_not_materialized"
    false
fi
audit_event "MANAGER_ROOM_CONFIGURED" "room_id=${ROOM_ID}"

# READY is an evidence-backed state, not a conversational claim.  A project
# created from another room must retain the exact source event, the verbatim
# request, its room event, and the deterministic migration notice.  Recovery
# must therefore rerun this script instead of manually editing lifecycle_state.
LAST_STAGE="READY_EVIDENCE"
if [ -n "${SOURCE_ROOM_ID}" ]; then
    RECORDED_SOURCE_EVENT_ID="$(jq -r '.source_event_id // empty' "${META_PATH}")"
    RECORDED_REQUEST_SHA="$(jq -r '.original_request_sha256 // empty' "${META_PATH}")"
    RECORDED_REQUEST_EVENT_ID="$(jq -r '.original_request_event_id // empty' "${META_PATH}")"
    RECORDED_NOTICE_EVENT_ID="$(jq -r '.source_notice_event_id // empty' "${META_PATH}")"
    if [ -z "${RECORDED_SOURCE_EVENT_ID}" ] || [ -z "${RECORDED_REQUEST_SHA}" ] || \
       [ -z "${RECORDED_REQUEST_EVENT_ID}" ] || [ -z "${RECORDED_NOTICE_EVENT_ID}" ] || \
       [ ! -s "${PROJECT_DIR}/request.md" ]; then
        LAST_ERROR="ready_evidence_incomplete"
        false
    fi
    if [ "${RECORDED_REQUEST_SHA}" != "$(printf '%s' "${ORIGINAL_REQUEST}" | sha256sum | cut -d ' ' -f1)" ]; then
        LAST_ERROR="original_request_hash_mismatch"
        false
    fi
    ORIGINAL_EVENT_ENC="$(jq -rn --arg value "${RECORDED_REQUEST_EVENT_ID}" '$value|@uri')"
    if ! curl -fsS "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/event/${ORIGINAL_EVENT_ENC}" \
        -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" | \
        jq -e --arg marker "[ORIGINAL_REQUEST] project_id=${PROJECT_ID}" \
        '.type=="m.room.message" and (.content.body|type)=="string" and (.content.body|contains($marker))' >/dev/null; then
        LAST_ERROR="original_request_event_not_observable"
        false
    fi
    SOURCE_NOTICE_EVENT_ENC="$(jq -rn --arg value "${RECORDED_NOTICE_EVENT_ID}" '$value|@uri')"
    SOURCE_ROOM_ENC="$(jq -rn --arg value "${SOURCE_ROOM_ID}" '$value|@uri')"
    if ! curl -fsS "${MATRIX_URL}/_matrix/client/v3/rooms/${SOURCE_ROOM_ENC}/event/${SOURCE_NOTICE_EVENT_ENC}" \
        -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" | \
        jq -e --arg marker "[PROJECT_MOVED] project_id=${PROJECT_ID}" \
        '.type=="m.room.message" and (.content.body|type)=="string" and (.content.body|contains($marker))' >/dev/null; then
        LAST_ERROR="source_notice_event_not_observable"
        false
    fi
fi

LAST_STAGE="READY"
READY_AT="$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
INITIALIZED_AT="$(jq -r '.room_initialized_at // empty' "${META_PATH}")"
if [ -z "${INITIALIZED_AT}" ]; then
    MESSAGE="@${ADMIN_USER}:${MATRIX_DOMAIN} [PROJECT_CREATED] project_id=${PROJECT_ID} lifecycle=READY。此房间是该项目需求、确认、派工、进度、异常与结项的唯一沟通空间。"
    TXN_ID="mcuforge-project-${PROJECT_ID}-$(date +%s%N)"
    curl -fsS -X PUT "${MATRIX_URL}/_matrix/client/v3/rooms/${ROOM_ENC}/send/m.room.message/${TXN_ID}" \
        -H "Authorization: Bearer ${MANAGER_MATRIX_TOKEN}" -H 'Content-Type: application/json' \
        -d "$(jq -cn --arg body "${MESSAGE}" --arg admin "${ADMIN_ID}" \
            '{msgtype:"m.text",body:$body,"m.mentions":{user_ids:[$admin]}}')" >/dev/null
    INITIALIZED_AT="${READY_AT}"
fi

jq --arg ready_at "${READY_AT}" --arg initialized_at "${INITIALIZED_AT}" \
    '.lifecycle_state="READY" | .room_ready_at=$ready_at | .room_initialized_at=$initialized_at
     | .updated_at=$ready_at | .last_error=null' "${META_PATH}" > "/tmp/${PROJECT_ID}-ready-$$.json"
mv "/tmp/${PROJECT_ID}-ready-$$.json" "${META_PATH}"
audit_event "READY" "room and members verified"
sync_project

trap - ERR
jq -n --arg id "${PROJECT_ID}" --arg title "${PROJECT_TITLE}" --arg room_id "${ROOM_ID}" \
    --arg room_alias "${ROOM_ALIAS}" --arg lifecycle "READY" --argjson workers "${WORKERS_JSON}" \
    '{project_id:$id,title:$title,project_room_id:$room_id,room_alias:$room_alias,
      interaction_mode:"project_room_only",lifecycle_state:$lifecycle,workers:$workers}'
