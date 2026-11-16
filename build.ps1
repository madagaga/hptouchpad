# Forwards to build.sh (Git Bash or WSL must be installed: Docker does the rest). Usage: .\build.ps1 [target] [--dirty]
# Targets: all | kernel | initramfs | rootfs | tp | ts-srv | bccmd | cog | release | menuconfig | shell | clean | help
$ErrorActionPreference = "Stop"
bash "$PSScriptRoot/build.sh" @args
exit $LASTEXITCODE
