#!/usr/bin/env bash

set -euo pipefail

mode="${1:-}"
argument="${2:-}"
failure=0

secret_pattern='-----BEGIN ([A-Z0-9]+ )*PRIVATE KEY-----|github_pat_[A-Za-z0-9_]{20,}|gh[pousr]_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|AIza[0-9A-Za-z_-]{30,}|xox[baprs]-[0-9A-Za-z-]{10,}|sk-(proj-)?[A-Za-z0-9_-]{20,}'

is_forbidden_path() {
    local path="${1//\\//}"
    local lower
    lower="$(printf '%s' "$path" | tr '[:upper:]' '[:lower:]')"

    case "$lower" in
        toolchains.md|*/toolchains.md|.codex-local/*|*/.codex-local/*)
            return 0
            ;;
        .env|*/.env|.env.*|*/.env.*)
            case "$lower" in
                .env.example|*/.env.example|*.example)
                    return 1
                    ;;
            esac
            return 0
            ;;
        deploy/secrets/*|*/deploy/secrets/*)
            case "$lower" in
                */deploy/secrets/.gitignore|deploy/secrets/.gitignore|*.example)
                    return 1
                    ;;
            esac
            return 0
            ;;
        *.pem|*.key|*.p12|*.pfx|*.kdbx|*.ovpn|id_rsa|*/id_rsa|id_ed25519|*/id_ed25519)
            return 0
            ;;
    esac

    return 1
}

scan_path() {
    local source="$1"
    local ref="$2"
    local path="$3"

    if is_forbidden_path "$path"; then
        printf 'Blocked sensitive path: %s\n' "$path" >&2
        failure=1
    fi

    if [[ "$source" == "staged" ]]; then
        if git show ":$path" 2>/dev/null | grep -Eaq -- "$secret_pattern"; then
            printf 'Potential embedded credential in staged file: %s\n' "$path" >&2
            failure=1
        fi
    elif git show "$ref:$path" 2>/dev/null | grep -Eaq -- "$secret_pattern"; then
        printf 'Potential embedded credential in commit %s, file: %s\n' "$ref" "$path" >&2
        failure=1
    fi
}

scan_staged() {
    local path
    while IFS= read -r path; do
        [[ -z "$path" ]] && continue
        scan_path staged "" "$path"
    done < <(git diff --cached --name-only --diff-filter=ACMR)
}

scan_tree() {
    local ref="$1"
    local path
    while IFS= read -r path; do
        [[ -z "$path" ]] && continue
        scan_path tree "$ref" "$path"
    done < <(git ls-tree -r --name-only "$ref")
}

scan_range() {
    local range="$1"
    local commit
    while IFS= read -r commit; do
        [[ -z "$commit" ]] && continue
        scan_tree "$commit"
    done < <(git rev-list "$range")
}

case "$mode" in
    staged)
        scan_staged
        ;;
    tree)
        [[ -n "$argument" ]] || {
            echo "tree mode requires a commit" >&2
            exit 2
        }
        scan_tree "$argument"
        ;;
    range)
        [[ -n "$argument" ]] || {
            echo "range mode requires a revision range" >&2
            exit 2
        }
        scan_range "$argument"
        ;;
    *)
        echo "usage: check-sensitive-content.sh staged|tree|range [revision]" >&2
        exit 2
        ;;
esac

if [[ "$failure" -ne 0 ]]; then
    echo "Sensitive-content guard failed." >&2
    exit 1
fi
