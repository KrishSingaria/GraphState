#!/usr/bin/env python3
"""Check for OSS-09: Required request parameter in RequestException."""

import inspect
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss09.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.exceptions import RequestException, ConnectionError, HTTPError
        from requests.models import PreparedRequest
    except ImportError as e:
        print(f"Failed to import requests.exceptions: {e}")
        sys.exit(1)

    req = PreparedRequest()
    req.prepare_url("http://example.com", {})

    # 1. Verify RequestException.__init__ requires request
    sig = inspect.signature(RequestException.__init__)
    if "request" not in sig.parameters:
        print("RequestException.__init__ missing 'request' parameter")
        sys.exit(1)

    # 2. Test instantiation without request raises TypeError
    try:
        exc_no_req = RequestException("error without request")
        # If it has a default, that violates the prompt requirement
        if exc_no_req.request is None:
            # Let's inspect parameter default
            p = sig.parameters["request"]
            if p.default != inspect.Parameter.empty:
                print(f"RequestException 'request' parameter must be required, found default: {p.default}")
                sys.exit(1)
    except TypeError:
        # Expected behavior when request is required
        pass

    # 3. Test instantiation with request succeeds
    try:
        exc = RequestException("sample error", request=req)
        if exc.request is not req:
            print("RequestException.request does not match provided request object")
            sys.exit(1)
    except Exception as e:
        print(f"Failed to instantiate RequestException with request: {e}")
        sys.exit(1)

    # 4. Test derived exceptions
    try:
        conn_err = ConnectionError("connection failed", request=req)
        if conn_err.request is not req:
            print("ConnectionError failed to preserve request object")
            sys.exit(1)
    except Exception as e:
        print(f"Derived ConnectionError failed: {e}")
        sys.exit(1)

    print("OSS-09 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
