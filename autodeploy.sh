#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2024-2026 OnlineCTR Team.
# Licensed under the GNU AGPL-3.0 (LICENSE), with the additional terms in
# ADDITIONAL-TERMS under its section 7. ABSOLUTELY NO WARRANTY.

# OnlineCTR server auto-deploy: rebuilds and restarts the server whenever a new
# release tag (v1103, v1104, ...) is published on GitHub. Run a copy of it from
# cron, kept OUTSIDE the server's checkout - checking out an older release
# would delete it, and a newer one would refuse to overwrite it (README):
#
#   octr-autodeploy.sh /path/to/server-checkout [--force]

set -e

# Go to the server's git checkout
SERVER_DIR="${1:?usage: $0 /path/to/server-checkout [--force]}"
cd "$SERVER_DIR"

# Fetch the release tags from GitHub (and drop any that were deleted there)
git fetch --quiet origin --tags --prune --prune-tags

# The newest release tag, ignoring any other tag
LATEST_TAG=$(git tag --list 'v*' --sort=-version:refname | head -n 1)
if [[ -z "$LATEST_TAG" ]]; then
    exit 0
fi

# The last tag deployed here
TAG_FILE=".last-deployed-tag"
PREV_TAG=$(cat "$TAG_FILE" 2>/dev/null || echo "")

# Nothing new: stop here, unless forced
if [[ "$LATEST_TAG" == "$PREV_TAG" && "${2:-}" != "--force" ]]; then
    exit 0
fi

echo "$(date '+%F %T') Deploying $LATEST_TAG (previous: ${PREV_TAG:-none})"

# Switch the code to exactly that release - not the newest commit on main
git checkout --quiet "$LATEST_TAG"

# Rebuild and restart the server
sudo docker compose up --build -d

# Remember the tag - only once the deploy has succeeded, so a failed one is
# retried on the next run
echo "$LATEST_TAG" > "$TAG_FILE"
echo "$(date '+%F %T') Deployed $LATEST_TAG"
