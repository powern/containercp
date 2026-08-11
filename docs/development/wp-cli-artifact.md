# Reviewed WP-CLI Artifact

ContainerCP uses the official WP-CLI Phar release artifact for version `2.11.0`.

| Field | Value |
|---|---|
| Release | `2.11.0` |
| Artifact | `wp-cli-2.11.0.phar` |
| Source | `https://github.com/wp-cli/wp-cli/releases/download/v2.11.0/wp-cli-2.11.0.phar` |
| SHA-256 | `a39021ac809530ea607580dbf93afbc46ba02f86b6cffd03de4b126ca53079f6` |

The reviewed version and SHA-256 are compiled into
`libs/wordpress/WordPressCliArtifactPolicy.h`. Runtime files under
`/srv/containercp/wp-cli/` are accepted only when their root-owned, read-only
metadata matches that policy and the actual Phar checksum matches it too.

The Phar is provisioned by ContainerCP installation/update tooling. Site
operations only validate and mount the already-provisioned artifact; they never
download WP-CLI.
