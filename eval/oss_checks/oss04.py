#!/usr/bin/env python3
"""Check for OSS-04: Structured tuple return in utils.requote_uri."""

import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss04.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.utils import requote_uri
        from requests.models import PreparedRequest
    except ImportError as e:
        print(f"Failed to import requests modules: {e}")
        sys.exit(1)

    # 1. Test requote_uri return type
    res = requote_uri("http://example.com/test path#frag")
    if not isinstance(res, tuple) or len(res) != 2:
        print(f"requote_uri expected to return (safe_uri, has_fragment), got {type(res)}: {res}")
        sys.exit(1)

    safe_uri, has_frag = res
    if not isinstance(safe_uri, str) or not isinstance(has_frag, bool):
        print(f"requote_uri tuple types invalid: (str, bool) expected, got ({type(safe_uri)}, {type(has_frag)})")
        sys.exit(1)

    if not has_frag:
        print("has_fragment should be True for URI containing '#frag'")
        sys.exit(1)

    # 2. Test PreparedRequest.prepare_url handles the tuple return cleanly
    req = PreparedRequest()
    try:
        req.prepare_url("http://example.com/api?a=1#section", {})
        if not isinstance(req.url, str):
            print(f"req.url expected to be string after prepare_url, got {type(req.url)}")
            sys.exit(1)
        if "example.com" not in req.url:
            print(f"req.url malformed: {req.url}")
            sys.exit(1)
    except Exception as e:
        print(f"PreparedRequest.prepare_url failed with updated requote_uri: {e}")
        sys.exit(1)

    print("OSS-04 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
