# Rust Migration Guide

The `sip-lab` project has been migrated from C/C++ to Rust. This guide explains how to build and test the new Rust-based Node.js addon.

## Prerequisites

The project still depends on several C-based SIP and media libraries. You must install the following system dependencies first:

```bash
sudo apt install build-essential automake autoconf libtool libspeex-dev libopus-dev \
libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev \
libopencore-amrwb-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev \
libssl-dev uuid-dev flite-dev cmake git wget
```

## Building

The Rust source code is located in the `rust/` directory.

### 1. Build the Rust Addon
Use `cargo` to compile the shared library:

```bash
cd rust
cargo build --release
```

### 2. Output Artifacts
The build process produces a shared library in `rust/target/release/`:
- **Linux**: `libsip_lab.so`
- **macOS**: `libsip_lab.dylib`
- **Windows**: `sip_lab.dll`

To use it with the Node.js application, rename or link the output to `sip_lab.node` in the project root:

```bash
cp rust/target/release/libsip_lab.so ./sip_lab.node
```

## Testing

### Rust Unit Tests
To test the ported Rust logic (e.g., `IdManager`):

```bash
cd rust
cargo test
```

### Node.js Integration Tests
Once the `.node` binary is in place, run the standard test suite:

```bash
npm test
```

## Project Structure

- `rust/src/napi_addon.rs`: The Node.js interface (replaces `addon.cpp`).
- `rust/src/sip_core.rs`: Ported business logic (replaces most of `sip.cpp`).
- `rust/src/sip_ffi.rs`: Foreign Function Interface declarations for the C stack.
- `rust/src/id_manager.rs`: Native Rust implementation of the ID tracker.
- `rust/src/event_templates.rs`: Native Rust implementation of JSON event generation.
