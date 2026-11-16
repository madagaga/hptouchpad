# Boots an image into RAM through novacom (persistent container; tablet in recovery mode
# and attached to WSL with usbipd).
param([string]$Image = "out/kernel/uImage")   # path relative to the repository root

docker exec -i hptouchpad-novacom sh -c 'novacom -l && novacom boot mem:// < "/work/$0"' $Image
exit $LASTEXITCODE
