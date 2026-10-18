#!/usr/bin/env python3
"""Check for OSS-08: timing_metrics in hooks.dispatch_hook."""

import inspect
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: oss08.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(run_dir, "src")
    if os.path.isdir(src_dir):
        sys.path.insert(0, src_dir)
    else:
        sys.path.insert(0, run_dir)

    try:
        from requests.hooks import dispatch_hook
    except ImportError as e:
        print(f"Failed to import dispatch_hook from requests.hooks: {e}")
        sys.exit(1)

    # 1. Verify dispatch_hook signature
    sig = inspect.signature(dispatch_hook)
    if "timing_metrics" not in sig.parameters:
        print("dispatch_hook missing 'timing_metrics' parameter")
        sys.exit(1)

    # 2. Test execution recording timing metrics
    metrics = {}
    def sample_hook(data):
        return data

    hooks = {"response": [sample_hook]}
    try:
        res = dispatch_hook("response", hooks, "payload", timing_metrics=metrics)
        if res != "payload":
            print(f"dispatch_hook returned unexpected data: {res}")
            sys.exit(1)
        if "response" not in metrics:
            print("dispatch_hook failed to record 'response' key into timing_metrics")
            sys.exit(1)
        if not isinstance(metrics["response"], (int, float)):
            print(f"timing metric duration is not a number: {metrics['response']}")
            sys.exit(1)
    except Exception as e:
        print(f"dispatch_hook execution failed: {e}")
        sys.exit(1)

    print("OSS-08 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
