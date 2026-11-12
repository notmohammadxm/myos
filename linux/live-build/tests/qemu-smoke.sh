#!/usr/bin/env bash
set -euo pipefail

iso="$(find . -maxdepth 1 -type f -name '*.iso' -print -quit)"
if [[ -z "$iso" || ! -s "$iso" ]]; then
  echo "::error::No non-empty live ISO found."
  exit 1
fi

log="$(pwd)/qemu-smoke.log"
rm -f "$log"

accel_args=()
cpu_args=(-cpu max)
if [[ -r /dev/kvm ]]; then
  accel_args=(-enable-kvm)
  cpu_args=(-cpu host)
fi

echo "Booting $iso in QEMU; waiting for desktop, browser and HTTPS markers."
set +e
timeout --signal=TERM --kill-after=5s 210s qemu-system-x86_64 \
  "${accel_args[@]}" \
  -m 4096 -smp 4 \
  "${cpu_args[@]}" \
  -cdrom "$iso" \
  -boot order=d \
  -vga std -display none -monitor none \
  -serial "file:$log" \
  -nic user,model=e1000
qemu_status=$?
set -e

required_markers=(
  MYOS_LINUX_BOOT_OK
  MYOS_BROWSER_OK
  MYOS_DESKTOP_OK
  MYOS_NETWORK_OK
  MYOS_LINUX_SMOKE_PASS
)
for marker in "${required_markers[@]}"; do
  if ! grep -q "$marker" "$log"; then
    echo "::error::QEMU smoke test did not report $marker (QEMU status: $qemu_status)."
    echo "----- QEMU serial log -----"
    cat "$log" || true
    exit 1
  fi
done

echo "QEMU desktop, Firefox ESR and HTTPS networking checks passed."
