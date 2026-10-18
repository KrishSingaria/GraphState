#!/usr/bin/env python3
"""Check for OSS-10: allow_async_chunks in utils.super_len."""

import inspect
import io
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss10.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.utils import super_len
        from requests.models import PreparedRequest
    except ImportError as e:
        print(f"Failed to import requests modules: {e}")
        sys.exit(1)

    # 1. Verify super_len signature
    sig = inspect.signature(super_len)
    if "allow_async_chunks" not in sig.parameters:
        print("super_len missing 'allow_async_chunks' parameter")
        sys.exit(1)

    # 2. Test standard buffer still works
    buf = io.BytesIO(b"standard payload")
    if super_len(buf) != 16:
        print(f"super_len standard BytesIO expected 16, got {super_len(buf)}")
        sys.exit(1)

    # 3. Test mock async stream wrapper with buffer
    class AsyncStreamWrapper:
        def __init__(self, data):
            self.buffer = data

        def read_chunk(self):
            return self.buffer

    wrapper = AsyncStreamWrapper(b"async buffer data 123")
    length = super_len(wrapper, allow_async_chunks=True)
    if length != 21:
        print(f"super_len with allow_async_chunks expected 21, got {length}")
        sys.exit(1)

    # 4. Test PreparedRequest.prepare_body does not error
    req = PreparedRequest()
    req.prepare_headers({})
    try:
        req.prepare_body(b"sample body content", None)
        if req.headers.get("Content-Length") != "19":
            print(f"PreparedRequest.prepare_body Content-Length unexpected: {req.headers.get('Content-Length')}")
            sys.exit(1)
    except Exception as e:
        print(f"PreparedRequest.prepare_body failed: {e}")
        sys.exit(1)

    print("OSS-10 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
