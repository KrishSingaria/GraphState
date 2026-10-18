#!/usr/bin/env python3
"""Check for OSS-02: preserve_casing in CaseInsensitiveDict."""

import inspect
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss02.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.structures import CaseInsensitiveDict
        from requests.models import PreparedRequest
    except ImportError as e:
        print(f"Failed to import requests modules: {e}")
        sys.exit(1)

    # 1. Verify CaseInsensitiveDict.__init__ signature
    sig = inspect.signature(CaseInsensitiveDict.__init__)
    if "preserve_casing" not in sig.parameters:
        print("CaseInsensitiveDict.__init__ missing 'preserve_casing' parameter")
        sys.exit(1)

    # 2. Test preserve_casing behavior
    d = CaseInsensitiveDict({"X-Custom-Header": "value1"}, preserve_casing=True)
    if hasattr(d, "preserve_casing") and not d.preserve_casing:
        print("preserve_casing attribute not preserved on CaseInsensitiveDict")
        sys.exit(1)

    # 3. Test copy preserves flag
    d_copy = d.copy()
    if hasattr(d_copy, "preserve_casing") and not d_copy.preserve_casing:
        print("CaseInsensitiveDict.copy() failed to propagate preserve_casing")
        sys.exit(1)

    # 4. Test PreparedRequest.prepare_headers caller
    req = PreparedRequest()
    try:
        req.prepare_headers({"Content-Type": "application/json"})
        if not isinstance(req.headers, CaseInsensitiveDict):
            print("req.headers is not a CaseInsensitiveDict")
            sys.exit(1)
    except Exception as e:
        print(f"PreparedRequest.prepare_headers failed: {e}")
        sys.exit(1)

    print("OSS-02 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
