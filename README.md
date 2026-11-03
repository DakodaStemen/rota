# Rota

Rota is a custom Linux distribution build for the Anbernic RG Rotate handheld console, featuring a trimmed kernel, Arch Linux ARM rootfs, and a QEMU-based CI test harness.

## Quick Start

- Build the OS image and components using `build/build.sh`:
  ```bash
  ./build/build.sh
  ```
- Run the QEMU CI self-test harness using `qemu/ci.sh`:
  ```bash
  ./qemu/ci.sh
  ```
