# bluecore6.psr

BlueCore6 (CSR) persistent store keys of the HP TouchPad: crystal trim, TX power table, PCM and so on. Loaded by `bt-power` with `bccmd`.

Taken **unmodified** from the CyanogenMod device tree, `CyanogenMod/android_device_hp_tenderloin`, branch `cm-11.0`, path
`bluetooth/bluecore6.psr` (last commit of that path: `dc3efeb6857e3564061bebedb1ff06f732c73697`, 2013-11-21), sha256
`2dd4643d17f22b0ff9ed3058d6b207dd8326f9e8a9b0855306ae0533771b5a41`. A copy is kept here so that the build does not depend on that
repository staying online. The repository declares no licence: see `docs/PUBLIC-RELEASE.md`.

The Dockerfile drops from it the example BD address and the UART keys (the address comes from the webOS token, see `bt-power`).
