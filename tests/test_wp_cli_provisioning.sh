#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
data_dir="$(mktemp -d "${TMPDIR:-/tmp}/containercp-wp-cli-provisioning.XXXXXX")"
cleanup() { rm -r "$data_dir"; }
trap cleanup EXIT

provision() {
    CONTAINERCP_DATA_DIR="$data_dir" bash "$root_dir/scripts/provision-wp-cli.sh"
}

artifact_dir="$data_dir/wp-cli"
provision

test "$(stat -c '%u:%g:%a' "$artifact_dir/wp-cli.phar")" = "0:0:444"
test "$(stat -c '%u:%g:%a' "$artifact_dir/version")" = "0:0:444"
test "$(stat -c '%u:%g:%a' "$artifact_dir/sha256")" = "0:0:444"
test "$(<"$artifact_dir/version")" = "2.11.0"
test "$(<"$artifact_dir/sha256")" = "a39021ac809530ea607580dbf93afbc46ba02f86b6cffd03de4b126ca53079f6"
test "$(sha256sum "$artifact_dir/wp-cli.phar" | cut -d' ' -f1)" = "a39021ac809530ea607580dbf93afbc46ba02f86b6cffd03de4b126ca53079f6"

before="$(stat -c '%Y' "$artifact_dir/wp-cli.phar")"
sleep 1
provision
test "$(stat -c '%Y' "$artifact_dir/wp-cli.phar")" = "$before"

printf 'corrupt' > "$artifact_dir/wp-cli.phar"
chmod 444 "$artifact_dir/wp-cli.phar"
provision
test "$(sha256sum "$artifact_dir/wp-cli.phar" | cut -d' ' -f1)" = "a39021ac809530ea607580dbf93afbc46ba02f86b6cffd03de4b126ca53079f6"

printf '2.12.0\n' > "$artifact_dir/version"
chmod 444 "$artifact_dir/version"
provision
test "$(<"$artifact_dir/version")" = "2.11.0"

printf '%064d\n' 0 > "$artifact_dir/sha256"
chmod 444 "$artifact_dir/sha256"
provision
test "$(<"$artifact_dir/sha256")" = "a39021ac809530ea607580dbf93afbc46ba02f86b6cffd03de4b126ca53079f6"

rm "$artifact_dir/wp-cli.phar"
ln -s /etc/hosts "$artifact_dir/wp-cli.phar"
provision
test -f "$artifact_dir/wp-cli.phar"
test ! -L "$artifact_dir/wp-cli.phar"

chmod 666 "$artifact_dir/wp-cli.phar"
provision
test "$(stat -c '%u:%g:%a' "$artifact_dir/wp-cli.phar")" = "0:0:444"

if grep -q 'latest' "$root_dir/scripts/provision-wp-cli.sh"; then
    exit 1
fi
