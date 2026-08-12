#!/bin/bash
# quote — wrapper so 'quote AAPL' works from Las_shell
# Resolves LAS_SHELL_HOME correctly both from build directory
# and from system install at /usr/local/share/las_shell

if [ -n "$LAS_SHELL_HOME" ]; then
    # Explicitly set — use as-is
    LHOME="$LAS_SHELL_HOME"
elif [ -f "$(dirname "$0")/quote.py" ]; then
    # Running from build directory: scripts/quote.sh sits next to quote.py
    # Resolve to the project root (one level up from scripts/)
    LHOME="$(cd "$(dirname "$0")/.." && pwd)"
else
    # Fallback for system install
    LHOME="/usr/local/share/las_shell"
fi

QUOTE_PY="$LHOME/scripts/quote.py"

if [ ! -f "$QUOTE_PY" ]; then
    echo "quote: $QUOTE_PY not found — check LAS_SHELL_HOME" >&2
    exit 1
fi

exec python3 "$QUOTE_PY" "$@"