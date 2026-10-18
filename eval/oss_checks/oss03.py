#!/usr/bin/env python3
"""Check for OSS-03: decoder parameter in Response.iter_content."""

import inspect
import io
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss03.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.models import Response
    except ImportError as e:
        print(f"Failed to import Response from requests.models: {e}")
        sys.exit(1)

    # 1. Verify Response.iter_content signature
    sig = inspect.signature(Response.iter_content)
    if "decoder" not in sig.parameters:
        print("Response.iter_content missing 'decoder' parameter")
        sys.exit(1)

    # 2. Test decoder invocation during iter_content
    r = Response()
    r.raw = io.BytesIO(b"hello world")
    r.status_code = 200

    def upper_decoder(chunk):
        return chunk.upper()

    try:
        chunks = list(r.iter_content(chunk_size=5, decoder=upper_decoder))
        combined = b"".join(chunks)
        if combined != b"HELLO WORLD":
            print(f"iter_content with decoder expected b'HELLO WORLD', got {combined}")
            sys.exit(1)
    except Exception as e:
        print(f"iter_content with decoder failed: {e}")
        sys.exit(1)

    # 3. Test Response.content property does not break
    r2 = Response()
    r2.raw = io.BytesIO(b"content test")
    r2.status_code = 200
    try:
        content = r2.content
        if content != b"content test":
            print(f"Response.content returned unexpected: {content}")
            sys.exit(1)
    except Exception as e:
        print(f"Response.content failed: {e}")
        sys.exit(1)

    print("OSS-03 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
