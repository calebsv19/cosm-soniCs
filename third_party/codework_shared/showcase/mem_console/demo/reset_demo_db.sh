#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
MEM_CLI="${ROOT_DIR}/shared/core/core_memdb/build/mem_cli"
DEFAULT_DATA_DIR="${HOME:-}/Desktop/CodeWork/data"

if [[ -n "${CODEWORK_MEMDB_PATH:-}" ]]; then
    DEFAULT_DB_PATH="${CODEWORK_MEMDB_PATH}"
elif [[ -n "${HOME:-}" ]] && [[ -d "${DEFAULT_DATA_DIR}" ]]; then
    DEFAULT_DB_PATH="${DEFAULT_DATA_DIR}/codework_mem_console.sqlite"
else
    DEFAULT_DB_PATH="${ROOT_DIR}/shared/showcase/mem_console/demo/demo_mem_console.sqlite"
fi

DB_PATH="${1:-${DEFAULT_DB_PATH}}"
DB_DIR="$(dirname "${DB_PATH}")"

mkdir -p "${DB_DIR}"

if [[ ! -x "${MEM_CLI}" ]]; then
    echo "mem_cli not found at ${MEM_CLI}" >&2
    echo "build it with: make -C shared/core/core_memdb all" >&2
    exit 1
fi

rm -f "${DB_PATH}"
"${MEM_CLI}" list --db "${DB_PATH}" >/dev/null

echo "Seeding baseline memory graph..."
add_memory() {
    local stable_id="$1"
    local title="$2"
    local body="$3"
    local project="$4"
    local kind="$5"
    local output
    local id
    output="$("${MEM_CLI}" add \
        --db "${DB_PATH}" \
        --stable-id "${stable_id}" \
        --title "${title}" \
        --body "${body}" \
        --project "${project}" \
        --kind "${kind}")"
    id="$(printf '%s\n' "${output}" | sed -n 's/.*id=\([0-9][0-9]*\).*/\1/p' | head -n 1)"
    if [[ -z "${id}" ]]; then
        echo "failed to parse id for ${stable_id}" >&2
        exit 1
    fi
    printf '%s' "${id}"
}

ID_NORTH_STAR="$(add_memory \
    "mem-console-north-star" \
    "Memory Console North Star" \
    "Canonical direction for the memory console: keep retrieval bounded, keep writes explicit, keep graph links intentional." \
    "memory_console" \
    "summary")"
ID_SCOPE="$(add_memory \
    "mem-console-scope-filters" \
    "Scope Filter Behavior" \
    "Project and workspace filters should constrain list retrieval and graph neighborhood reads." \
    "memory_console" \
    "design")"
ID_ASYNC="$(add_memory \
    "mem-console-async-refresh" \
    "Async Refresh Loop" \
    "Wake-driven refresh coalescing keeps UI responsive while avoiding redundant DB reload churn." \
    "memory_console" \
    "runtime")"
ID_LINK_POLICY="$(add_memory \
    "memdb-link-policy" \
    "Link Policy Guardrails" \
    "Allowed kinds are canonical-only and self-loops are rejected at CLI write time." \
    "memory_db" \
    "policy")"
ID_CONSOLE_PLAN="$(add_memory \
    "mem-console-upgrade-plan" \
    "Console Upgrade Plan" \
    "Next upgrades focus on graph visibility, project lanes, and bounded depth decisions." \
    "memory_console" \
    "plan")"
ID_IDE_DIR="$(add_memory \
    "scope-ide" \
    "IDE Tag Directory" \
    "Tracks IDE-specific memory slices and editor workflow notes." \
    "ide" \
    "scope")"
ID_FISICS_DIR="$(add_memory \
    "scope-fisics" \
    "Fisics Tag Directory" \
    "Tracks fisics simulation notes, runtime findings, and rendering checks." \
    "fisics" \
    "scope")"
ID_BEHAVIOR_DIR="$(add_memory \
    "scope-behavior" \
    "Behavior Tag Directory" \
    "Tracks behavior-level rules, agent habits, and workflow policies." \
    "behavior" \
    "scope")"

"${MEM_CLI}" canonical --db "${DB_PATH}" --id "${ID_NORTH_STAR}" --on >/dev/null
"${MEM_CLI}" pin --db "${DB_PATH}" --id "${ID_NORTH_STAR}" --on >/dev/null
"${MEM_CLI}" pin --db "${DB_PATH}" --id "${ID_CONSOLE_PLAN}" --on >/dev/null

"${MEM_CLI}" link-add --db "${DB_PATH}" --from "${ID_SCOPE}" --to "${ID_NORTH_STAR}" --kind supports >/dev/null
"${MEM_CLI}" link-add --db "${DB_PATH}" --from "${ID_ASYNC}" --to "${ID_SCOPE}" --kind depends_on >/dev/null
"${MEM_CLI}" link-add --db "${DB_PATH}" --from "${ID_LINK_POLICY}" --to "${ID_NORTH_STAR}" --kind references >/dev/null
"${MEM_CLI}" link-add --db "${DB_PATH}" --from "${ID_CONSOLE_PLAN}" --to "${ID_NORTH_STAR}" --kind summarizes >/dev/null
"${MEM_CLI}" link-add --db "${DB_PATH}" --from "${ID_CONSOLE_PLAN}" --to "${ID_ASYNC}" --kind related >/dev/null
"${MEM_CLI}" link-add --db "${DB_PATH}" --from "${ID_IDE_DIR}" --to "${ID_CONSOLE_PLAN}" --kind related >/dev/null
"${MEM_CLI}" link-add --db "${DB_PATH}" --from "${ID_FISICS_DIR}" --to "${ID_ASYNC}" --kind related >/dev/null
"${MEM_CLI}" link-add --db "${DB_PATH}" --from "${ID_BEHAVIOR_DIR}" --to "${ID_LINK_POLICY}" --kind supports >/dev/null

echo "Reset complete: ${DB_PATH}"
echo "Row count:"
"${MEM_CLI}" list --db "${DB_PATH}" | wc -l | awk '{print "rows listed:", $1}'
