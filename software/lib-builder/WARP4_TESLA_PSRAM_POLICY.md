WARP4 Tesla ordinary allocation policy (source-review only)

Stock defconfig.esp32secure and the conservative Bluetooth overlay remain unchanged.
The explicit tesla-psram-first-v1 policy adds exactly:
    CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: 2048 -> 0
The additional overlay is defconfig.warp4-tesla-psram-first. No other security,
partition, flash, PSRAM speed/layout, Bluetooth allocation or task-stack delta is
allowed. Protected resolved defaults are recorded from the pinned official secure
SDK config (digest in defconfig.warp4-protected-resolved); they constrain generated
aliases/defaults as well as explicitly configured symbols. Unknown changed
protected keys are rejected. Header values must exactly match effective config.

Explicit build from software/:
    .venv/bin/python lib-builder/tesla_sdk.py prepare --build --policy tesla-psram-first-v1
    TESLA_BLE_FRAMEWORK_LIBS="$PWD/.pio/tesla-sdk/builder/out/tools/esp32-arduino-libs" .venv/bin/python -m platformio run -e warp4_tesla_ble_verifier_review -t tesla-review

Recipe CLI and generic validators default to stock (2048). Tesla review guard and
its native VFS acceptance explicitly select tesla-psram-first-v1. The standalone
installer semantic validator requires the caller's explicit policy and a complete
explained delta inventory; its default remains stock. Selecting a policy is not
SDK approval. Existing production approval locks must not be reissued implicitly.
The review manifest remains noninstallable and preserves unresolved baseline pins.
Old SDK manifests/snapshots are evidence for their old recipe/artifacts only.

Implementation nuance verified on the actual candidate:
CONFIG_SPIRAM_BOOT_INIT=n: Arduino psramAddToHeap (compiled in FrameworkArduino)
calls heap_caps_malloc_extmem_enable(0). IDF libheap reads that dynamic limit;
libesp_psram's boot-init branch is disabled. A freshly rebuilt IDF libheap/libesp_psram
may therefore be byte-identical to the old archives: do not claim these archives
changed merely because the threshold changed. Verify the actual framework object
and retained final ELF call argument, not only sdkconfig strings.
For nonzero ordinary allocations, PSRAM is preferred after heap registration,
with existing default-capability fallback on allocation failure. Explicit INTERNAL
(including INTERNAL|8BIT) and DMA capability requests are unaffected. 8BIT by itself
is not an internal-placement guarantee. External task stacks remain disabled;
BT_ALLOCATION_FROM_SPIRAM_FIRST remains disabled. No blanket allocator wrapper or
bench configuration is used.

Compile/link, static ELF/MAP sizes and disassembly do not establish runtime placement,
heap savings, largest free block, fragmentation, RF stability or stack watermarks.
No installable app/OTA image, signing, device operation or release is authorized.
