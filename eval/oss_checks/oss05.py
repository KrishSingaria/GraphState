#!/usr/bin/env python3
"""Check for OSS-05: Request context in HTTPBasicAuth.__call__."""

import inspect
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss05.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.auth import HTTPBasicAuth, HTTPProxyAuth
        from requests.models import PreparedRequest
    except ImportError as e:
        print(f"Failed to import auth modules: {e}")
        sys.exit(1)

    # 1. Verify signatures
    sig_basic = inspect.signature(HTTPBasicAuth.__call__)
    if "context" not in sig_basic.parameters:
        print("HTTPBasicAuth.__call__ missing 'context' parameter")
        sys.exit(1)

    sig_proxy = inspect.signature(HTTPProxyAuth.__call__)
    if "context" not in sig_proxy.parameters:
        print("HTTPProxyAuth.__call__ missing 'context' parameter")
        sys.exit(1)

    # 2. Test execution with context
    req = PreparedRequest()
    req.prepare_url("http://user:pass@example.com", {})
    req.prepare_headers({})

    auth = HTTPBasicAuth("testuser", "testpass")
    try:
        auth(req, context={"host": "example.com"})
        if "Authorization" not in req.headers:
            print("Authorization header not set by HTTPBasicAuth with context")
            sys.exit(1)
    except Exception as e:
        print(f"Calling HTTPBasicAuth with context failed: {e}")
        sys.exit(1)

    # 3. Test PreparedRequest.prepare_auth invokes auth properly
    req2 = PreparedRequest()
    req2.prepare_url("http://example.com/login", {})
    req2.prepare_headers({})
    try:
        req2.prepare_auth(auth)
        if "Authorization" not in req2.headers:
            print("PreparedRequest.prepare_auth failed to apply auth")
            sys.exit(1)
    except Exception as e:
        print(f"PreparedRequest.prepare_auth failed: {e}")
        sys.exit(1)

    print("OSS-05 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
