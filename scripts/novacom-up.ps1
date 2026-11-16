# Starts (or restarts) the persistent container that runs novacomd.
# Prerequisites: the tablet is shared (`usbipd bind`, admin) and attached
# (`usbipd attach --wsl --busid <id>`).
$Name = "hptouchpad-novacom"
docker rm -f $Name 2>$null | Out-Null
docker run -d --name $Name --restart unless-stopped --privileged `
    -v /dev/bus/usb:/dev/bus/usb `
    -v "$($PSScriptRoot)\..:/work" `
    hptouchpad-novacom novacomd
Start-Sleep 2
docker exec $Name novacom -l
