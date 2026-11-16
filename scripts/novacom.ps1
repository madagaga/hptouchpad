# Runs novacom inside the persistent container (see novacom-up.ps1).
# Usage: .\scripts\novacom.ps1 -l
#        .\scripts\novacom.ps1 get file:///proc/cpuinfo
#        .\scripts\novacom.ps1 run file:///bin/uname -a
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$NovacomArgs)

docker exec -i hptouchpad-novacom novacom @NovacomArgs
exit $LASTEXITCODE
