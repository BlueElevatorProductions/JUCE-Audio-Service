# Testing Lazy-Load Behavior

The lazy-load behavior is implemented with the following logic:

## Test Case 1: Render with file loaded via LoadFile first (existing behavior)
```bash
# This should work as before
./grpc_client_cli load --path test.wav
./grpc_client_cli render --path test.wav --out output.wav
```

## Test Case 2: Render without calling LoadFile first (new lazy-load)
```bash
# This should now work thanks to lazy-loading
./grpc_client_cli render --path test.wav --out output.wav
```

## Test Case 3: Render with no file loaded and no input path (error case)
```bash
# This should fail with "NO_FILE_LOADED" error
./grpc_client_cli render --out output.wav
```

## Test Case 4: Render with invalid input path (lazy-load failure)
```bash
# This should fail with "LAZY_LOAD_FAILED" error
./grpc_client_cli render --path nonexistent.wav --out output.wav
```

## Expected Log Messages:
- Success: "[gRPC] Lazy-loaded input for render: /absolute/path/to/test.wav"
- No file provided: "[gRPC] Render failed: no file loaded and no input file provided"
- Invalid file: "[gRPC] Render failed: lazy-load failed - File not found: /path/to/nonexistent.wav"
