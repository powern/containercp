#!/usr/bin/env bash
set -euo pipefail

DATA_DIR="${CONTAINERCP_DATA_DIR:-/srv/containercp}"
ARTIFACT_DIR="$DATA_DIR/wp-cli"
VERSION="2.11.0"
EXPECTED_SHA="a39021ac809530ea607580dbf93afbc46ba02f86b6cffd03de4b126ca53079f6"
RELEASE_URL="https://github.com/wp-cli/wp-cli/releases/download/v2.11.0/wp-cli-2.11.0.phar"

is_safe_file() {
    local path="$1"
    [ -f "$path" ] && [ ! -L "$path" ] && [ "$(stat -c '%u:%g:%a' "$path")" = "0:0:444" ]
}

is_current_bundle() {
    local actual_sha installed_version installed_sha
    is_safe_file "$ARTIFACT_DIR/wp-cli.phar" || return 1
    is_safe_file "$ARTIFACT_DIR/version" || return 1
    is_safe_file "$ARTIFACT_DIR/sha256" || return 1
    installed_version="$(<"$ARTIFACT_DIR/version")"
    installed_sha="$(<"$ARTIFACT_DIR/sha256")"
    actual_sha="$(sha256sum "$ARTIFACT_DIR/wp-cli.phar" | cut -d' ' -f1)"
    [ "$installed_version" = "$VERSION" ] &&
        [ "$installed_sha" = "$EXPECTED_SHA" ] &&
        [ "$actual_sha" = "$EXPECTED_SHA" ]
}

if [ -L "$ARTIFACT_DIR" ]; then
    echo "[ERROR] Refusing to provision through a symlinked WP-CLI directory" >&2
    exit 1
fi

mkdir -p "$ARTIFACT_DIR"
chown root:root "$ARTIFACT_DIR"
chmod 755 "$ARTIFACT_DIR"

if is_current_bundle; then
    echo "[SYSTEM] Reviewed WP-CLI $VERSION is already provisioned"
    exit 0
fi

temporary_dir="$(mktemp -d "${TMPDIR:-/tmp}/containercp-wp-cli.XXXXXX")"
cleanup() { rm -r "$temporary_dir"; }
trap cleanup EXIT

echo "[SYSTEM] Provisioning reviewed WP-CLI $VERSION"
curl --fail --location --retry 2 --silent --show-error "$RELEASE_URL" \
    --output "$temporary_dir/wp-cli.phar"
actual_sha="$(sha256sum "$temporary_dir/wp-cli.phar" | cut -d' ' -f1)"
if [ "$actual_sha" != "$EXPECTED_SHA" ]; then
    echo "[ERROR] Downloaded WP-CLI artifact SHA-256 does not match reviewed policy" >&2
    exit 1
fi

printf '%s\n' "$VERSION" > "$temporary_dir/version"
printf '%s\n' "$EXPECTED_SHA" > "$temporary_dir/sha256"
install -o root -g root -m 0444 "$temporary_dir/wp-cli.phar" "$ARTIFACT_DIR/.wp-cli.phar.new"
install -o root -g root -m 0444 "$temporary_dir/version" "$ARTIFACT_DIR/.version.new"
install -o root -g root -m 0444 "$temporary_dir/sha256" "$ARTIFACT_DIR/.sha256.new"
mv -fT "$ARTIFACT_DIR/.wp-cli.phar.new" "$ARTIFACT_DIR/wp-cli.phar"
mv -fT "$ARTIFACT_DIR/.version.new" "$ARTIFACT_DIR/version"
mv -fT "$ARTIFACT_DIR/.sha256.new" "$ARTIFACT_DIR/sha256"

if ! is_current_bundle; then
    echo "[ERROR] Provisioned WP-CLI bundle failed local validation" >&2
    exit 1
fi
echo "[SYSTEM] Reviewed WP-CLI $VERSION provisioned"
