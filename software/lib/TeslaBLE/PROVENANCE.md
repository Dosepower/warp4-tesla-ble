# TeslaBLE provenance

- Upstream project: https://github.com/yoziru/tesla-ble
- Upstream author/maintainer: Yasir Ekinci
- Upstream version: 5.2.0
- Pinned revision: `7486ebfe4673c40b28264a75cee2fb98af444eec`
- Imported for: optional WARP4 Tesla BLE SoC integration

The upstream package metadata declares MIT, but the `LICENSE` distributed in
the same revision contains the GNU Affero General Public License version 3.
This vendored copy therefore uses the conservative SPDX expression
`AGPL-3.0-only` pending clarification from the upstream author.

Local packaging changes:

- Renamed the PlatformIO package to `TeslaBLE-WARP`.
- Pinned nanopb to 0.4.92.
- Included generated protobuf sources in the PlatformIO build.
- No Tesla BLE charge-start behavior is called by the WARP integration.