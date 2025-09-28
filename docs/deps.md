# Dependency Management

This document describes how dependencies are managed in the JUCE Audio Service project.

## Platform Requirements

**This project requires Apple Silicon (arm64) macOS only.**

- Apple Silicon Mac (M1, M2, M3, etc.)
- macOS 12.0 or later
- Git LFS for binary caching
- CMake 3.20+

## JUCE Framework

The project uses JUCE v7.0.12 as a git submodule for deterministic builds without network dependencies at build time.

### Automatic Setup

For most users, dependencies are automatically handled:

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/BlueElevatorProductions/JUCE-Audio-Service.git

# Or initialize submodules after cloning
git submodule update --init --recursive

# Build (automatically calls get_deps.sh)
./scripts/build.sh
```

### Manual Setup

If you need to set up dependencies manually or if network access is restricted:

#### Option 1: Initialize Submodules

```bash
git submodule update --init --recursive
```

#### Option 2: Manual JUCE Installation

If network access is blocked or submodules cannot be used:

1. Download JUCE v7.0.12 from: https://github.com/juce-framework/JUCE/releases/tag/7.0.12
2. Extract the archive
3. Copy the JUCE source to `third_party/JUCE/` in your project directory
4. Ensure the directory structure looks like:
   ```
   third_party/JUCE/
   ├── CMakeLists.txt
   ├── modules/
   ├── extras/
   └── ...
   ```

#### Option 3: Custom JUCE Location

If you have JUCE installed elsewhere:

```bash
# Set environment variable
export JUCE_SOURCE_DIR=/path/to/your/juce

# Or use CMake option
cmake -S . -B build -DUSE_LOCAL_JUCE=OFF -DJUCE_SOURCE_DIR=/path/to/your/juce
```

### Updating JUCE Version

To update to a different JUCE version:

1. Update the submodule:
   ```bash
   cd third_party/JUCE
   git fetch --tags
   git checkout tags/NEW_VERSION -b juce-NEW_VERSION
   cd ../..
   git add third_party/JUCE
   git commit -m "Update JUCE to NEW_VERSION"
   ```

2. Update the default version in `scripts/get_deps.sh`:
   ```bash
   JUCE_TAG=${JUCE_TAG:-NEW_VERSION}
   ```

### Verification

You can verify your JUCE installation by running:

```bash
./scripts/get_deps.sh
```

This will show the current JUCE version and commit hash.

### Troubleshooting

#### "JUCE not found" error

If you get a CMake error about JUCE not being found:

1. Run `./scripts/get_deps.sh` to set up dependencies
2. If that fails, try initializing submodules: `git submodule update --init --recursive`
3. As a last resort, manually install JUCE as described above

#### Network issues

If you're behind a firewall or have network restrictions:

1. Download JUCE manually and place it in `third_party/JUCE/`
2. Or ask your IT department to allow access to `github.com`

#### CI/CD Issues

If CI builds fail:

1. Ensure your CI configuration includes `submodules: true` in the checkout step
2. Check that the runner has network access to GitHub
3. Consider using dependency caching in your CI pipeline

## gRPC Dependencies (Optional)

When `ENABLE_GRPC=ON` is specified, the project uses vcpkg to manage gRPC and Protocol Buffers dependencies.

### vcpkg Setup

vcpkg is included as a git submodule and pinned to a stable release:

```bash
# vcpkg submodule is automatically initialized with:
git submodule update --init --recursive
```

### Binary Caching

The project uses a local file-based binary cache to avoid rebuilding gRPC on every machine:

- Cache location: `third_party/vcpkg_cache/`
- Cache is tracked in Git LFS
- First build downloads and caches gRPC/protobuf
- Subsequent builds use cached binaries (much faster)

### Setup for gRPC Development

1. Install Git LFS:
   ```bash
   git lfs install
   git lfs pull
   ```

2. Build with gRPC enabled:
   ```bash
   ./scripts/build.sh Release -DENABLE_GRPC=ON
   ```

3. After first successful build with gRPC, commit the cache:
   ```bash
   git add third_party/vcpkg_cache/
   git commit -m "Add vcpkg binary cache for gRPC dependencies"
   ```

### Environment Variables

The following environment variables are automatically set by `scripts/vcpkg_env.sh`:

- `CMAKE_OSX_ARCHITECTURES=arm64`
- `MACOSX_DEPLOYMENT_TARGET=12.0`
- `VCPKG_TARGET_TRIPLET=arm64-osx`
- `VCPKG_HOST_TRIPLET=arm64-osx`
- `VCPKG_BUILD_TYPE=release`
- `VCPKG_DEFAULT_BINARY_CACHE=./third_party/vcpkg_cache`
- `VCPKG_BINARY_SOURCES=clear;files,./third_party/vcpkg_cache,readwrite`

### Offline Builds

Once the binary cache is populated and committed to LFS:

- Local builds work offline
- CI/CD builds work offline (after LFS pull)
- Codex builds work offline (after LFS pull)

### Updating gRPC Version

To update gRPC or protobuf versions:

1. Update `vcpkg.json` with new version constraints
2. Clear the cache: `rm -rf third_party/vcpkg_cache/*`
3. Rebuild with gRPC enabled
4. Commit the new cache

## Build System

The project uses CMake with the following dependency-related options:

- `USE_LOCAL_JUCE=ON` (default): Use JUCE from `third_party/JUCE/`
- `USE_LOCAL_JUCE=OFF`: Use JUCE from `JUCE_SOURCE_DIR` environment variable or CMake variable
- `ENABLE_GRPC=OFF` (default): Build without gRPC support
- `ENABLE_GRPC=ON`: Build with gRPC server and client (requires Apple Silicon)

The build script `scripts/build.sh` automatically calls `scripts/get_deps.sh` to ensure dependencies are available before building. When `ENABLE_GRPC=ON` is detected in the arguments, it automatically configures the vcpkg toolchain.