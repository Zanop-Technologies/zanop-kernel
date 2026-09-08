#!/usr/bin/env bash
# =====================================================================
# Zanop Kernel — run-qemu.sh
# Builds the kernel and boots it in QEMU for a manual smoke test.
#
# Requirements (install these yourself, not bundled here):
#   - Rust nightly with rust-src component (see rust-toolchain.toml)
#   - nasm
#   - qemu-system-x86_64
#   - grub-mkrescue + xorriso (to build a bootable ISO from the
#     Multiboot2 binary produced by the kernel build)
#
# Usage:
#   ./scripts/run-qemu.sh
# =====================================================================

set -euo pipefail

KERNEL_BIN="target/x86_64-zanop/debug/zanop-kernel"
ISO_DIR="target/iso"
ISO_OUT="target/zanop-os.iso"

echo "==> Building kernel..."
cargo build

echo "==> Assembling boot ISO..."
rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/boot/grub"
cp "$KERNEL_BIN" "$ISO_DIR/boot/kernel.bin"

cat > "$ISO_DIR/boot/grub/grub.cfg" <<EOF
set timeout=0
set default=0

menuentry "Zanop OS" {
    multiboot2 /boot/kernel.bin
    boot
}
EOF

grub-mkrescue -o "$ISO_OUT" "$ISO_DIR"

echo "==> Booting in QEMU..."
qemu-system-x86_64 \
  -cdrom "$ISO_OUT" \
  -serial stdio \
  -no-reboot \
  -no-shutdown
  