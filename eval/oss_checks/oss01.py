#!/usr/bin/env python3
"""Check for OSS-01: Explicit connection_timeout parameter in BaseAdapter.send."""

import inspect
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss01.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.adapters import BaseAdapter, HTTPAdapter
        from requests.sessions import Session, SessionRedirectMixin
        from requests.models import PreparedRequest
    except ImportError as e:
        print(f"Failed to import requests modules: {e}")
        sys.exit(1)

    # 1. Verify BaseAdapter.send signature
    sig_base = inspect.signature(BaseAdapter.send)
    params_base = list(sig_base.parameters.values())
    param_names_base = [p.name for p in params_base]
    if "connection_timeout" not in param_names_base:
        print("BaseAdapter.send does not have 'connection_timeout' parameter")
        sys.exit(1)

    idx_conn = param_names_base.index("connection_timeout")
    if idx_conn != 2:
        print(f"BaseAdapter.send 'connection_timeout' expected at position 2, got position {idx_conn}")
        sys.exit(1)

    # 2. Verify HTTPAdapter.send signature
    sig_http = inspect.signature(HTTPAdapter.send)
    params_http = list(sig_http.parameters.values())
    param_names_http = [p.name for p in params_http]
    if "connection_timeout" not in param_names_http:
        print("HTTPAdapter.send does not have 'connection_timeout' parameter")
        sys.exit(1)

    # 3. Test Session.send caller passing connection_timeout
    class MockAdapter(BaseAdapter):
        def __init__(self):
            super().__init__()
            self.received_conn_timeout = None

        def send(self, request, connection_timeout=None, **kwargs):
            self.received_conn_timeout = connection_timeout
            from requests.models import Response
            r = Response()
            r.status_code = 200
            r.request = request
            return r

    s = Session()
    mock_adapter = MockAdapter()
    s.mount("http://mock.test", mock_adapter)

    req = PreparedRequest()
    req.prepare_url("http://mock.test/api", {})
    req.prepare_method("GET")
    req.prepare_headers({})

    try:
        resp = s.send(req, connection_timeout=3.5)
        if mock_adapter.received_conn_timeout != 3.5:
            print(f"Session.send failed to forward connection_timeout: got {mock_adapter.received_conn_timeout}")
            sys.exit(1)
    except TypeError as e:
        print(f"TypeError during Session.send: {e}")
        sys.exit(1)

    print("OSS-01 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
