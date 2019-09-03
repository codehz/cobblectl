#!/bin/bash
set -euo pipefail
url_cobblestone="https://hertz.services/docker/codehz/cobblestone/0"
url_bds="https://hertz.services/docker/codehz/bds/0"
url_nsgod="https://hertz.services/github/codehz/nsgod/latest/nsgod"
url_cobblectl="https://hertz.services/github/codehz/cobblectl/latest/cobblectl"

mkdir -p .cobblestone/{core,game}
tempdir=$(mktemp -d)
echo "[+] created temp directory: ${tempdir}"

dialog="dialog"
if ! [ -x "$(command -v dialog)" ]; then
  dialog=whiptail
fi

cmd=($dialog --separate-output --checklist "Select options:" 12 60 6)
options=(1 "install nsgod" on # any option can be set to default to "on"
  2 "install cobblestone" on
  3 "install bds" on
  4 "install cobblectl" on)
choices=$("${cmd[@]}" "${options[@]}" 2>&1 >/dev/tty)
clear
for choice in $choices; do
  case $choice in
  1)
    echo "[=] downloading nsgod"
    wget -O ".cobblestone/nsgod" ${url_nsgod}
    chmod +x .cobblestone/nsgod
    ;;
  2)
    echo "[=] downloading cobblestone"
    wget -O "${tempdir}/cobblestone.tar.gz" ${url_cobblestone}

    echo "[=] extracting cobblestone"
    tar xvf "${tempdir}/cobblestone.tar.gz" -C .cobblestone/core
    ;;
  3)
    echo "[=] downloading bds"
    wget -O "${tempdir}/bds.tar.gz" ${url_bds}

    echo "[=] extracting bds"
    tar xvf "${tempdir}/bds.tar.gz" -C .cobblestone/game
    ;;
  4)
    echo "[=] downloading cobblectl"
    wget -O "cobblectl" ${url_cobblectl}
    chmod +x cobblectl
    echo "Create a folder at first ( mkdir test )"
    echo "Usage: ./cobblectl start --wait test"
    echo "Stop the server: ./cobblectl stop --wait test"
    echo "To use cli: ./cobblectl attach test"
  esac
done
exit
