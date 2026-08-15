#!/usr/bin/env python3
"""Generate pairs trading orders from spread signal."""
import os, sys, subprocess, re, fcntl

lhome    = os.environ.get("LAS_SHELL_HOME", ".")
logdir   = os.environ.get("LOGDIR", "logs")
pair_a   = os.environ.get("PAIR_A", "AAPL")
pair_b   = os.environ.get("PAIR_B", "MSFT")
pos_size = int(os.environ.get("POS_SIZE", 500))
entry_z  = float(os.environ.get("ENTRY_Z", 2.0))
exit_z   = float(os.environ.get("EXIT_Z", 0.5))

# FIX GP1: PAIR_A/PAIR_B were interpolated unsanitized into shell command
# strings (command injection, e.g. PAIR_A="AAPL; rm -rf ~") and into an
# f-string /tmp path used for direction-state persistence (path traversal,
# e.g. PAIR_A="../../etc/cron.d/evil"). Validate once, up front.
_TICKER_RE = re.compile(r"^[A-Za-z0-9.\-]{1,16}$")
for _name, _val in (("PAIR_A", pair_a), ("PAIR_B", pair_b)):
    if not _TICKER_RE.match(_val):
        print(f"{_name}={_val!r} is not a valid ticker symbol", file=sys.stderr)
        sys.exit(1)

def run_price_feed(*args):
    """Run a scripts/*.py helper via argv (no shell), return its stdout."""
    proc = subprocess.run(
        ["python3", *args],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        text=True, timeout=10,
    )
    return proc.stdout

result = run_price_feed(
    f"{lhome}/scripts/pairs_spread.py", pair_a, pair_b, "--lookback", "60"
).strip()

if not result:
    print("No spread data", file=sys.stderr)
    sys.exit(1)

parts  = dict(item.split("=",1) for item in result.split() if "=" in item)
signal = parts.get("SIGNAL", "HOLD")
zscore = float(parts.get("ZSCORE", 0))

def get_price(t):
    r = run_price_feed(f"{lhome}/scripts/price_feed.py", t).split()
    return float(r[-1]) if r else 100.0

pa = get_price(pair_a)
pb = get_price(pair_b)

STATE_DIR = os.path.join(os.environ.get("HOME", "."), ".las_shell", "state")

def _state_path(name):
    """FIX GP2: private, non-world-writable directory instead of /tmp.
    Unlike the streaming FIFO fix (F7), this can't use a random-suffixed
    path -- this file has to be found again by a later, separate run of
    this same script (no shared secret to pass between invocations), so
    the defense here is location + open flags, not unpredictability."""
    os.makedirs(STATE_DIR, mode=0o700, exist_ok=True)
    return os.path.join(STATE_DIR, name)

def load_direction():
    state_file = _state_path(f"pairs_{pair_a}_{pair_b}.txt")
    try:
        # O_NOFOLLOW: refuse to follow a symlink planted at this path.
        fd = os.open(state_file, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(fd) as f:
            # FIX GP8: exclusive lock for the read, released on close.
            # Blocks briefly if a concurrent invocation is mid-write.
            fcntl.flock(f.fileno(), fcntl.LOCK_EX)
            try:
                return f.read().strip()
            finally:
                fcntl.flock(f.fileno(), fcntl.LOCK_UN)
    except OSError:
        return None

def save_direction(direction):
    state_file = _state_path(f"pairs_{pair_a}_{pair_b}.txt")
    try:
        fd = os.open(state_file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    except OSError as e:
        print(f"gen_pairs_orders: refusing to write {state_file}: {e}", file=sys.stderr)
        sys.exit(1)
    with os.fdopen(fd, "w") as f:
        # FIX GP8: exclusive lock for the write, released on close.
        fcntl.flock(f.fileno(), fcntl.LOCK_EX)
        try:
            f.write(direction)
        finally:
            fcntl.flock(f.fileno(), fcntl.LOCK_UN)


orders = []
if signal == f"LONG_{pair_a}_SHORT_{pair_b}":
    orders = [f"{pair_a} BUY {pos_size} {pa:.2f}", f"{pair_b} SELL {pos_size} {pb:.2f}"]
    save_direction(f"LONG_{pair_a}_SHORT_{pair_b}")
elif signal == f"LONG_{pair_b}_SHORT_{pair_a}":
    orders = [f"{pair_b} BUY {pos_size} {pb:.2f}", f"{pair_a} SELL {pos_size} {pa:.2f}"]
    save_direction(f"LONG_{pair_b}_SHORT_{pair_a}")
elif signal == "EXIT":
    direction = load_direction()
    if direction == f"LONG_{pair_b}_SHORT_{pair_a}":
        orders = [f"{pair_a} BUY {pos_size} {pa:.2f}", f"{pair_b} SELL {pos_size} {pb:.2f}"]
    else:
        orders = [f"{pair_a} SELL {pos_size} {pa:.2f}", f"{pair_b} BUY {pos_size} {pb:.2f}"]

if not orders:
    print(f"Signal: HOLD (z={zscore:.2f}) — no trade")
    sys.exit(0)

print(f"Signal: {signal} (z={zscore:.2f})")
for o in orders:
    print(f"  ORDER: {o}")

os.makedirs(logdir, exist_ok=True)
with open(f"{logdir}/pairs_orders.txt", "w") as f:
    f.write("\n".join(o.strip() for o in orders) + "\n")
sys.exit(0)