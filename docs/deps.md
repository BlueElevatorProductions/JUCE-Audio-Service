# Dependency Management

This document describes how dependencies are managed in the JUCE Audio Service project.

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

## Build System

The project uses CMake with the following dependency-related options:

- `USE_LOCAL_JUCE=ON` (default): Use JUCE from `third_party/JUCE/`
- `USE_LOCAL_JUCE=OFF`: Use JUCE from `JUCE_SOURCE_DIR` environment variable or CMake variable

The build script `scripts/build.sh` automatically calls `scripts/get_deps.sh` to ensure dependencies are available before building.