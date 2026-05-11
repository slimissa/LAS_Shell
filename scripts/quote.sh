#!/bin/bash
# quote — wrapper so 'quote AAPL' works from Las_shell
LHOME="${LAS_SHELL_HOME:-/usr/local/share/las_shell}"
exec python3 "$LHOME/scripts/quote.py" "$@"