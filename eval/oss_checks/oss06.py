#!/usr/bin/env python3
"""Check for OSS-06: allow_insecure_redirects in Session.request pipeline."""

import inspect
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss06.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.sessions import Session
    except ImportError as e:
        print(f"Failed to import Session from requests.sessions: {e}")
        sys.exit(1)

    s = Session()

    # 1. Check Session.request signature
    sig_req = inspect.signature(s.request)
    if "allow_insecure_redirects" not in sig_req.parameters:
        print("Session.request missing 'allow_insecure_redirects' parameter")
        sys.exit(1)

    # 2. Check verb helpers accept allow_insecure_redirects
    for method_name in ["get", "post", "head", "delete"]:
        meth = getattr(s, method_name, None)
        if not meth:
            print(f"Session missing method '{method_name}'")
            sys.exit(1)
        # Verify kwargs forwarding or signature
        sig = inspect.signature(meth)
        has_param = "allow_insecure_redirects" in sig.parameters or any(
            p.kind == inspect.Parameter.VAR_KEYWORD for p in sig.parameters.values()
        )
        if not has_param:
            print(f"Session.{method_name} cannot accept allow_insecure_redirects")
            sys.exit(1)

    print("OSS-06 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
