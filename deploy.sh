#!/bin/bash
# Payload Manager - Automated Build & Deploy Script

if [ -z "$1" ]; then
    echo "Usage: ./deploy.sh [PS5_IP]"
    exit 1
fi

PS5_IP="$1"
MENU_PORT="8911" # [CN-FORK] Web 端口，上游默认 8084，与 pldmgr.h MENU_PORT 保持一致
LOADER_PORT="9021"
# [CN-FORK] ELF 产物名带版本号，版本取自 pldmgr.h MENU_VERSION
VERSION=$(grep '#define MENU_VERSION' include/pldmgr.h | awk '{print $3}' | tr -d '"')
ELF="NayePlmTools_${VERSION}.elf"

echo "--- Deploying NayePlmTools to $PS5_IP ---"

# 1. Build the React Frontend
echo "[1/3] Building React Frontend..."
make frontend-build > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "      !!! Frontend build FAILED!"
    exit 1
fi
echo "      Frontend build successful."

# 2. Build the native ELF via Docker
echo "[2/3] Building native ELF via Docker..."
docker run --rm -v "$(pwd)":/src -w /src ps5-payload-sdk-pldmgr make clean all > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "      !!! ELF build FAILED! Check Makefile or source errors."
    exit 1
fi
echo "      ELF build successful."

# 3. Send to PS5
if [ -f "$ELF" ]; then
    echo "[3/3] Sending $ELF to $PS5_IP:$LOADER_PORT via socat..."
    socat -u - TCP:$PS5_IP:$LOADER_PORT < "$ELF"
    if [ $? -eq 0 ]; then
        echo "--- Deployment Complete! ---"
    else
        echo "      !!! Failed to send ELF. Is the loader running on PS5?"
        exit 1
    fi
else
    echo "      !!! $ELF not found!"
    exit 1
fi
