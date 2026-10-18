#!/usr/bin/env python3
"""Check for OSS-07: Explicit domain_specified keyword argument in create_cookie."""

import inspect
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss07.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.cookies import create_cookie, cookiejar_from_dict, RequestsCookieJar
    except ImportError as e:
        print(f"Failed to import requests.cookies: {e}")
        sys.exit(1)

    # 1. Verify create_cookie signature
    sig = inspect.signature(create_cookie)
    if "domain_specified" not in sig.parameters:
        print("create_cookie missing 'domain_specified' parameter")
        sys.exit(1)

    # 2. Test create_cookie invocation
    try:
        c = create_cookie("session_token", "abc123xyz", domain_specified=True, domain="example.com")
        if c.name != "session_token" or c.value != "abc123xyz":
            print("create_cookie returned incorrect cookie attributes")
            sys.exit(1)
    except Exception as e:
        print(f"create_cookie failed: {e}")
        sys.exit(1)

    # 3. Test cookiejar_from_dict caller
    try:
        jar = cookiejar_from_dict({"test": "123"})
        if not isinstance(jar, RequestsCookieJar):
            print("cookiejar_from_dict did not return a RequestsCookieJar")
            sys.exit(1)
        if "test" not in jar:
            print("cookiejar_from_dict failed to store cookie")
            sys.exit(1)
    except Exception as e:
        print(f"cookiejar_from_dict caller failed: {e}")
        sys.exit(1)

    print("OSS-07 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
