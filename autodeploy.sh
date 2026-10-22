#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2024-2026 OnlineCTR Team.
# Licensed under the GNU AGPL-3.0 (LICENSE), with the additional terms in
# ADDITIONAL-TERMS under its section 7. ABSOLUTELY NO WARRANTY.

# OnlineCTR server auto-deploy: rebuilds and restarts the server whenever a new
# release tag (v1113, v1114, ...) signed by the OnlineCTR team is published on
# GitHub. Run a copy of it from cron, kept OUTSIDE the server's checkout -
# checking out an older release would delete it, and a newer one would refuse
# to overwrite it (README):
#
#   octr-autodeploy.sh /path/to/server-checkout [--force]
#
# --force redeploys the release already deployed here, if it is signed.

set -e

# The OnlineCTR team's release key, by its whole fingerprint (gpg
# --fingerprint; spaces are fine). Every release tag is signed with it, and
# only a tag with a good signature from this key, under its own name, is
# deployed (README, Signed releases). Pinned here, in the host's own copy of
# the script, rather than trusting whatever keys the host's keyring holds: any
# other key imported for any other reason would otherwise pass too. Empty:
# nothing is deployed. The key ("OnlineCTR release key", from v1113 on) is
# octr-release-key.asc in the repository; the host imports it once.
RELEASE_KEY_FINGERPRINT="67A0 CFA6 CD94 39CA 1C10  E3B3 94BD 15F6 A659 1B53"

log()  { echo "$(date '+%F %T') $*"; }
warn() { echo "$(date '+%F %T') $*" >&2; }

# Go to the server's git checkout
SERVER_DIR="${1:?usage: $0 /path/to/server-checkout [--force]}"
FORCE=0
[[ "${2:-}" == "--force" ]] && FORCE=1
cd "$SERVER_DIR"

# The pinned fingerprint: one whole fingerprint (40 hex digits, or 64 for a
# v5 key), never a short key ID, which many keys share
FPR="${RELEASE_KEY_FINGERPRINT//[[:space:]]/}"
FPR="${FPR^^}"
if [[ -z "$FPR" ]]; then
    warn "RELEASE_KEY_FINGERPRINT is not set in this script: nothing deployed (see README, Signed releases)"
    exit 1
fi
if [[ ! "$FPR" =~ ^([0-9A-F]{40}|[0-9A-F]{64})$ ]]; then
    warn "RELEASE_KEY_FINGERPRINT is not a whole fingerprint (40 or 64 hex digits): nothing deployed (see README, Signed releases)"
    exit 1
fi

# Fetch the release tags from GitHub (and drop any that were deleted there).
# Its output is kept for a failure, which is said in the log with the way out:
# a release tag moved or re-signed on GitHub makes git refuse to replace the
# host's copy, on every run, until the host fetches with --force once.
if ! FETCH_OUT=$(git fetch origin --tags --prune --prune-tags 2>&1); then
    warn "git fetch failed: ${FETCH_OUT:-no message}"
    warn "If a release tag was moved on GitHub, run once: git -C $SERVER_DIR fetch --tags --force origin (see README, Signed releases)"
    exit 1
fi

# The last tag deployed here
TAG_FILE=".last-deployed-tag"
PREV_TAG=$(cat "$TAG_FILE" 2>/dev/null || echo "")

# A release this host will run: a signed tag object whose own name is the
# tag's (a tag pointed at another release's signed tag object is not that
# release), with a good signature (git verify-tag) from the pinned key.
# git verify-tag --raw prints gpg's status lines; VALIDSIG's third field is
# the signing key's fingerprint, its last the primary key's (for a subkey),
# compared whole, as text. REASON says why not.
release_ok() {
    local tag="$1" status name
    name=$(git for-each-ref --format='%(tag)' "refs/tags/$tag")
    if [[ "$name" != "$tag" ]]; then
        REASON="it is not a signed tag of that name"
        return 1
    fi
    if ! status=$(git verify-tag --raw "$tag" 2>&1); then
        REASON="its signature does not verify"
        return 1
    fi
    if ! awk -v f="$FPR" '$1 == "[GNUPG:]" && $2 == "VALIDSIG" && ($3 "" == f "" || $NF "" == f "") { ok = 1 } END { exit !ok }' <<< "$status"; then
        REASON="it is not signed with the release key $FPR"
        return 1
    fi
}

# Newer than the release deployed here (or nothing deployed yet)
newer() {
    [[ -z "$PREV_TAG" ]] && return 0
    [[ "$1" != "$PREV_TAG" ]] &&
        [[ "$(printf '%s\n' "$PREV_TAG" "$1" | sort -V | tail -n 1)" == "$1" ]]
}

# From the newest release tag down: the first one that is a signed release is
# deployed. One that is not is skipped, and said so on every run until it is
# deleted on GitHub (the next fetch drops it) or a signed one comes above it.
# Never anything older than the release deployed here: the walk stops there,
# so a newer tag deleted on GitHub cannot roll the host back. --force
# redeploys that release, if it is a signed one.
DEPLOY=""
SKIPPED=0
REASON=""
while read -r TAG; do
    [[ -n "$TAG" ]] || continue

    if [[ "$TAG" == "$PREV_TAG" ]]; then
        if (( FORCE )); then
            if release_ok "$TAG"; then
                DEPLOY="$TAG"
            else
                warn "$TAG not redeployed: $REASON (see README, Signed releases)"
                SKIPPED=1
            fi
        fi
        break
    fi

    if ! newer "$TAG"; then
        break
    fi

    if release_ok "$TAG"; then
        DEPLOY="$TAG"
        break
    fi
    warn "$TAG skipped: $REASON (see README, Signed releases)"
    SKIPPED=1
done < <(git tag --list 'v*' --sort=-version:refname)

# --force with nothing to redeploy because the deployed release's tag is gone
# from GitHub (the fetch pruned it): said, rather than a silent run
if [[ -z "$DEPLOY" ]] && (( FORCE )) && [[ -n "$PREV_TAG" ]] &&
   ! git rev-parse -q --verify "refs/tags/$PREV_TAG" >/dev/null; then
    warn "$PREV_TAG not redeployed: there is no such release tag any more (see README, Signed releases)"
    SKIPPED=1
fi

# Nothing to deploy: quietly when there is nothing new, with a failure when a
# tag was skipped, so the log and cron say so
if [[ -z "$DEPLOY" ]]; then
    exit "$SKIPPED"
fi

log "Deploying $DEPLOY (previous: ${PREV_TAG:-none})"

# Switch the code to exactly that release - not the newest commit on main
git checkout --quiet "refs/tags/$DEPLOY"

# Rebuild and restart the server
sudo docker compose up --build -d

# Remember the tag - only once the deploy has succeeded, so a failed one is
# retried on the next run
echo "$DEPLOY" > "$TAG_FILE"
log "Deployed $DEPLOY"

# A tag skipped on the way down fails the run even when a release below it
# was deployed, so every run with a bad tag on top says so the same way
exit "$SKIPPED"
