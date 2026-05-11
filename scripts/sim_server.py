#!/usr/bin/env python3
"""
sim_server.py — Las_shell Phase 4.2: Local Paper Broker Simulation Server
═══════════════════════════════════════════════════════════════════════

Implements the Generic REST adapter schema (Alpaca-compatible) so that
any Las_shell command that calls out via $BROKER_API can be tested without
a real broker account.

Usage
─────
    # Terminal 1 — start the server
    python3 sim_server.py [--port 8080] [--capital 100000] [--verbose]

    # Terminal 2 — configure Las_shell to use it
    setaccount PAPER
    setenv BROKER_API http://localhost:8080
    order buy SPY 100 market
    positions
    balance

Endpoints implemented
─────────────────────
    POST   /orders                 → place order
    GET    /orders                 → list open orders
    DELETE /orders/:id             → cancel order
    GET    /positions              → position table (JSON)
    GET    /positions/:symbol      → single position
    GET    /account                → account equity

Price feed
──────────
    Delegates to scripts/quote.py for realistic simulated prices.
    Falls back to a deterministic in-memory price if quote.py is
    unavailable.

State persistence
─────────────────
    ~/.las_shell_sim_server_state.json — reloaded on restart.
"""

import argparse
import json
import os
import subprocess
import sys
import time
import threading
import hashlib
import math
import random
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

# ── Base prices for the fallback price feed ──────────────────────────────
BASE_PRICES = {
    "AAPL": 185.0,  "MSFT": 415.0,  "GOOGL": 175.0, "AMZN": 195.0,
    "TSLA": 175.0,  "META": 510.0,  "NVDA":  875.0, "SPY":  510.0,
    "QQQ":  435.0,  "GLD":  195.0,  "IWM":   200.0, "BTC":  67000.0,
    "ETH":  3500.0, "NFLX": 620.0,  "PYPL":   65.0, "AMD":  165.0,
    "INTC":  30.0,  "JPM":  195.0,  "GS":    465.0, "BAC":   37.0,
}

STATE_FILE = os.path.expanduser("~/.las_shell_sim_server_state.json")

# ── Global mutable state (protected by _lock) ─────────────────────────────
_lock         = threading.Lock()
_state: dict  = {}          # loaded / initialised in main()
_verbose      = False

# ─────────────────────────────────────────────────────────────────────────
# Price feed
# ─────────────────────────────────────────────────────────────────────────

def _sim_price(ticker: str) -> float:
    """Deterministic per-minute price using a seeded random walk."""
    ticker = ticker.upper()
    base   = BASE_PRICES.get(ticker, 100.0)
    day_seed = int(hashlib.md5(
        f"{ticker}{datetime.now().strftime('%Y%m%d')}".encode()).hexdigest(), 16)
    random.seed(day_seed)
    daily_ret = random.gauss(0.0002, 0.012)
    close_base = base * (1.0 + daily_ret)
    min_seed = int(hashlib.md5(
        f"{ticker}{datetime.now().strftime('%Y%m%d%H%M')}".encode()).hexdigest(), 16)
    random.seed(min_seed)
    return round(close_base * (1.0 + random.gauss(0, 0.002)), 2)

def get_price(ticker: str) -> float:
    """Try scripts/quote.py first; fall back to sim_price."""
    try:
        result = subprocess.run(
            ["python3", "scripts/quote.py", ticker],
            capture_output=True, text=True, timeout=2
        )
        line = result.stdout.strip()
        if line:
            parts = line.split()
            val = float(parts[-1])
            if val > 0:
                return val
    except Exception:
        pass
    return _sim_price(ticker)

# ─────────────────────────────────────────────────────────────────────────
# State persistence
# ─────────────────────────────────────────────────────────────────────────

def _default_state(capital: float) -> dict:
    return {
        "cash":          capital,
        "starting_cash": capital,
        "positions":     {},   # symbol → {qty, avg_cost, realized_pnl}
        "orders":        {},   # order_id → order dict
        "order_seq":     1000,
        "fills":         [],   # history list
    }

def _save_state():
    try:
        with open(STATE_FILE, "w") as f:
            json.dump(_state, f, indent=2)
    except OSError as e:
        _log(f"[sim_server] WARNING: cannot save state: {e}")

def _load_state(capital: float) -> dict:
    if os.path.exists(STATE_FILE):
        try:
            with open(STATE_FILE) as f:
                loaded = json.load(f)
            _log(f"[sim_server] Loaded state from {STATE_FILE}")
            return loaded
        except Exception as e:
            _log(f"[sim_server] WARNING: corrupt state file ({e}), starting fresh")
    return _default_state(capital)

def _log(msg: str):
    if _verbose:
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        print(f"{ts}  {msg}", flush=True)

# ─────────────────────────────────────────────────────────────────────────
# Order engine
# ─────────────────────────────────────────────────────────────────────────

def _iso_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

def _place_order(symbol: str, qty: int, side: str,
                 order_type: str, limit_price: float,
                 time_in_force: str) -> dict:
    """
    Execute an order against the in-memory ledger.
    Returns an order dict (matches the generic adapter's expected JSON).
    """
    symbol = symbol.upper()
    fill_price = get_price(symbol) if order_type == "market" else limit_price
    notional   = fill_price * qty

    _state["order_seq"] += 1
    oid = str(_state["order_seq"])
    ts  = _iso_now()

    pos = _state["positions"].setdefault(
        symbol, {"qty": 0, "avg_cost": 0.0, "realized_pnl": 0.0}
    )

    if side == "buy":
        if _state["cash"] < notional:
            order = {
                "id": oid, "symbol": symbol, "qty": str(qty), "side": side,
                "type": order_type, "time_in_force": time_in_force,
                "status": "rejected", "filled_qty": "0",
                "filled_avg_price": None,
                "fill_price": None,
                "reject_reason": f"insufficient_cash (need {notional:.2f}, have {_state['cash']:.2f})",
                "created_at": ts,
            }
            _log(f"  REJECTED buy {symbol} qty={qty}: insufficient cash")
            _state["orders"][oid] = order
            _save_state()
            return order

        # Weighted-average cost basis
        old_cost = pos["avg_cost"] * pos["qty"]
        pos["qty"]      += qty
        pos["avg_cost"]  = (old_cost + notional) / pos["qty"] if pos["qty"] else fill_price
        _state["cash"]  -= notional

    else:  # sell
        if pos["qty"] < qty:
            order = {
                "id": oid, "symbol": symbol, "qty": str(qty), "side": side,
                "type": order_type, "time_in_force": time_in_force,
                "status": "rejected", "filled_qty": "0",
                "filled_avg_price": None, "fill_price": None,
                "reject_reason": f"insufficient_position (have {pos['qty']}, selling {qty})",
                "created_at": ts,
            }
            _log(f"  REJECTED sell {symbol} qty={qty}: insufficient position")
            _state["orders"][oid] = order
            _save_state()
            return order

        proceeds            = fill_price * qty
        pos["realized_pnl"] += (fill_price - pos["avg_cost"]) * qty
        pos["qty"]          -= qty
        _state["cash"]      += proceeds

    order = {
        "id":               oid,
        "symbol":           symbol,
        "qty":              str(qty),
        "side":             side,
        "type":             order_type,
        "time_in_force":    time_in_force,
        "status":           "filled",
        "filled_qty":       str(qty),
        "filled_avg_price": str(round(fill_price, 4)),
        "fill_price":       str(round(fill_price, 4)),
        "notional":         str(round(notional, 2)),
        "created_at":       ts,
        "filled_at":        ts,
    }

    _state["orders"][oid] = order
    _state["fills"].append({
        "ts": ts, "side": side, "symbol": symbol,
        "qty": qty, "fill_price": fill_price, "notional": notional,
    })
    _save_state()
    _log(f"  FILLED {side.upper()} {symbol} qty={qty} @ {fill_price:.4f}  cash={_state['cash']:.2f}")
    return order

def _get_positions() -> list:
    result = []
    for sym, p in _state["positions"].items():
        if p["qty"] == 0:
            continue
        mkt = get_price(sym)
        mktval = mkt * p["qty"]
        up     = (mkt - p["avg_cost"]) * p["qty"]
        result.append({
            "symbol":           sym,
            "qty":              str(p["qty"]),
            "avg_entry_price":  str(round(p["avg_cost"], 4)),
            "market_price":     str(round(mkt, 4)),
            "market_value":     str(round(mktval, 2)),
            "unrealized_pl":    str(round(up, 2)),
            "unrealized_plpc":  str(round(up / (p["avg_cost"] * p["qty"]) if p["qty"] else 0, 4)),
            "realized_pl":      str(round(p["realized_pnl"], 2)),
        })
    return result

def _get_account() -> dict:
    tot_mkt = sum(
        get_price(s) * p["qty"]
        for s, p in _state["positions"].items() if p["qty"] > 0
    )
    equity   = _state["cash"] + tot_mkt
    pnl      = equity - _state["starting_cash"]
    bp       = _state["cash"]
    return {
        "id":            "sim-account-001",
        "status":        "ACTIVE",
        "mode":          "PAPER",
        "cash":          str(round(_state["cash"], 2)),
        "buying_power":  str(round(bp, 2)),
        "equity":        str(round(equity, 2)),
        "market_value":  str(round(tot_mkt, 2)),
        "total_pnl":     str(round(pnl, 2)),
        "starting_cash": str(round(_state["starting_cash"], 2)),
        "currency":      "USD",
        "updated_at":    _iso_now(),
    }

# ─────────────────────────────────────────────────────────────────────────
# HTTP handler
# ─────────────────────────────────────────────────────────────────────────

class BrokerHandler(BaseHTTPRequestHandler):
    """Minimal HTTP/1.1 REST handler for the sim broker."""

    def log_message(self, fmt, *args):
        if _verbose:
            super().log_message(fmt, *args)

    def _send_json(self, code: int, payload):
        body = json.dumps(payload, indent=2).encode()
        self.send_response(code)
        self.send_header("Content-Type",   "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_error(self, code: int, message: str):
        self._send_json(code, {"message": message, "code": code})

    def _read_body(self) -> dict:
        length = int(self.headers.get("Content-Length", 0))
        if length == 0:
            return {}
        raw = self.rfile.read(length)
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            return {}

    # ── GET ─────────────────────────────────────────────────────────────
    def do_GET(self):
        parsed = urlparse(self.path)
        path   = parsed.path.rstrip("/")

        with _lock:
            # GET /positions
            if path == "/positions":
                self._send_json(200, _get_positions())

            # GET /positions/:symbol
            elif path.startswith("/positions/"):
                sym = path.split("/")[-1].upper()
                all_pos = _get_positions()
                found = [p for p in all_pos if p["symbol"] == sym]
                if found:
                    self._send_json(200, found[0])
                else:
                    self._send_error(404, f"position not found: {sym}")

            # GET /account  (also /v2/account for Alpaca compat)
            elif path in ("/account", "/v2/account"):
                self._send_json(200, _get_account())

            # GET /orders
            elif path in ("/orders", "/v2/orders"):
                open_orders = [
                    o for o in _state["orders"].values()
                    if o["status"] not in ("filled", "rejected", "cancelled")
                ]
                self._send_json(200, open_orders)

            else:
                self._send_error(404, f"endpoint not found: {path}")

    # ── POST ────────────────────────────────────────────────────────────
    def do_POST(self):
        parsed = urlparse(self.path)
        path   = parsed.path.rstrip("/")
        body   = self._read_body()

        if path not in ("/orders", "/v2/orders"):
            self._send_error(404, f"endpoint not found: {path}")
            return

        # Validate required fields
        symbol = (body.get("symbol") or "").upper()
        qty_raw = body.get("qty", 0)
        side    = (body.get("side") or "").lower()
        otype   = (body.get("type") or "market").lower()
        tif     = (body.get("time_in_force") or "day").lower()
        try:
            qty = int(qty_raw)
        except (ValueError, TypeError):
            self._send_error(422, "qty must be an integer"); return

        if not symbol:
            self._send_error(422, "symbol required"); return
        if side not in ("buy", "sell"):
            self._send_error(422, f"side must be buy or sell, got: {side}"); return
        if qty <= 0:
            self._send_error(422, "qty must be > 0"); return

        limit = 0.0
        try:
            limit = float(body.get("limit_price") or 0)
        except (ValueError, TypeError):
            pass

        if otype in ("limit", "stop_limit") and limit <= 0:
            self._send_error(422, "limit_price required for limit orders"); return

        _log(f"  POST /orders  {side} {symbol} qty={qty} type={otype} tif={tif} limit={limit}")

        with _lock:
            order = _place_order(symbol, qty, side, otype, limit, tif)

        if order["status"] == "rejected":
            self._send_json(422, order)
        else:
            self._send_json(201, order)

    # ── DELETE ──────────────────────────────────────────────────────────
    def do_DELETE(self):
        parsed = urlparse(self.path)
        path   = parsed.path.rstrip("/")

        # DELETE /orders/:id  or  /v2/orders/:id
        if "/orders/" in path:
            oid = path.split("/")[-1]
            with _lock:
                if oid not in _state["orders"]:
                    self._send_error(404, f"order not found: {oid}"); return
                order = _state["orders"][oid]
                if order["status"] == "filled":
                    self._send_error(422, "cannot cancel a filled order"); return
                order["status"] = "cancelled"
                _save_state()
            _log(f"  CANCELLED order {oid}")
            self.send_response(204)
            self.end_headers()
        else:
            self._send_error(404, f"endpoint not found: {path}")

# ─────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────

def main():
    global _state, _verbose

    parser = argparse.ArgumentParser(
        description="Las_shell paper broker simulation server")
    parser.add_argument("--port",    type=int,   default=8080,
                        help="TCP port to listen on (default: 8080)")
    parser.add_argument("--capital", type=float, default=100000.0,
                        help="Starting paper capital (default: 100000)")
    parser.add_argument("--reset",   action="store_true",
                        help="Wipe existing state and start fresh")
    parser.add_argument("--verbose", action="store_true",
                        help="Log every request and fill")
    args = parser.parse_args()

    _verbose = args.verbose

    if args.reset and os.path.exists(STATE_FILE):
        os.unlink(STATE_FILE)
        print(f"[sim_server] State reset. Starting capital: ${args.capital:,.2f}")

    _state = _load_state(args.capital)

    server = HTTPServer(("127.0.0.1", args.port), BrokerHandler)

    print(f"[sim_server] Las_shell paper broker listening on http://127.0.0.1:{args.port}")
    print(f"[sim_server] Starting cash: ${_state['cash']:,.2f}  |  state: {STATE_FILE}")
    print(f"[sim_server] In Las_shell:  setenv BROKER_API http://localhost:{args.port}")
    print(f"[sim_server] Press Ctrl+C to stop.\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[sim_server] Shutting down.")
        server.shutdown()

if __name__ == "__main__":
    main()
