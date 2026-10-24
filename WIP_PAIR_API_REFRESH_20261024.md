# WIP: Guided API / confirmed discovery refresh — source review only

SOURCE REVIEW ONLY — DO NOT INSTALL. No new firmware binary is published with this checkpoint.
Based on the existing Preview 4 source (4ca6c1c0b4b05c02e01ba6aa6e3ffdcbc959ea51), without changing default or historical release branches/tags.

Changes: complete guided phase payloads including revision; monotonic backend revision/identity checks; concrete API errors; bounded VIN-bound discovery refresh only inside an explicitly confirmed Pair/Poll action when saved discovery expired; strict scope, fresh unique address/type and save/readback/consent checks. Unchanged inactive profile retention grants no radio authority. No automatic pairing, polling or charging.

The five changed production files and relevant tests were extracted from the sealed offline-qualified candidate, not copied from a mutable working tree wholesale. Installer internals, evidence ledgers, reports, receipts, firmware, ELF/MAP and device artifacts are deliberately excluded. A legacy test fallback path is made home-relative; set TESLA_BLE_SDK_SOURCE_ROOT explicitly for repeatability. VIN/MAC literals in tests are synthetic fixtures, including pre-existing published fixtures, not vehicle identities.

Qualification of the sealed local candidate: 19 local stages, 59/59 wrapper entries, normal and sanitizer host suites, authored/generated frontend tests, native Config schema boundary (5 accepted / 101 rejected), 14 mounted-browser cases, full review compile/link and exact embedded-web byte checks passed. These are local software results, not vehicle acceptance. This curated publication checkout additionally reruns its available guided/schema regressions; no claim that a new full firmware was built from this Git commit.

Real pairing, vehicle key approval, SoC retrieval, runtime memory/coexistence and longevity remain untested for these fixes. No device/LAN access, OTA, restart or charging operation occurred in publication.

Original SDK backup is separate from source Git history. The verified public build-support archive set is available at https://github.com/Dosepower/warp4-tesla-ble/releases/tag/build-support-original-nimble-sdk-20261024 and preserves all 1,974 original manifest-listed payload files plus the unchanged manifest. All seven assets were downloaded back from GitHub, their hashes compared, and the extracted SDK/core/source-policy and complete file manifest verified. Compiler/host runtimes are not archived; exact toolchain download pins and test/isolation support accompany the SDK. Generic original build paths remain in debug data as disclosed in the release README.

The publication checkout's native schema, authored guided frontend and actual EV mount tests passed after installing locked frontend dependencies and locally restoring the existing generated enum prerequisites. Generated files and test outputs are not pushed. The source-derived replacement used for the sealed candidate remains distinct from the byte-identical recovered original SDK.
