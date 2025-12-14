#!/usr/bin/env bash
set -euo pipefail

# Simple harness to build and run the COW regression under QEMU.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${ROOT_DIR}/obj/cowtest.log"

cd "${ROOT_DIR}"

echo "[1/3] Cleaning old outputs (make clean)..."
make clean >/dev/null

echo "[2/3] Building kernel with TEST=cowtest..."
make build-cowtest >/dev/null

echo "[3/3] Booting cowtest under QEMU (timeout 20s)..."
QEMU_BIN="${QEMU:-qemu-system-riscv64}"
if ! timeout 20s "${QEMU_BIN}" \
    -machine virt \
    -nographic \
    -bios default \
    -device loader,file=bin/ucore.img,addr=0x80200000 \
    >"${LOG_FILE}" 2>&1; then
        echo "QEMU run failed. Full log at ${LOG_FILE}:"
        tail -n 40 "${LOG_FILE}" || true
        exit 1
    fi

if grep -q "COW test pass." "${LOG_FILE}"; then
    echo "COW test succeeded. Key output:"
    grep -E "kernel_execve|child wrote|parent still|COW test pass" "${LOG_FILE}"
else
    echo "COW test did not report success. Full log at ${LOG_FILE}:"
    tail -n 80 "${LOG_FILE}" || true
    exit 1
fi
