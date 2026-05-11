# Las_shell — Quantitative Trading Shell V0.5.0

> A production-grade Unix shell where a trading strategy **is** a pipeline.  
> Built in C on Linux. 16 quant features across 4 phases. 508 tests. Zero failures.

```bash
@09:30:00 universe SPY constituents \
  | momentum_filter --lookback 20 \
  | risk_filter --max-pos 1000 \
  | size_positions --capital $CAPITAL \
  ?> risk_check.py \
  | execute |> trades.csv

@15:55:00 flatten
```

To backtest the same strategy over 4 years — change exactly one token:

```bash
  | execute ~> 2020-01-01:2023-12-31
```

---

## Quick Start

```bash
# Get a price
las_quote AAPL

# Interactive paper trading
las_shell
work
setmarket NYSE
setaccount PAPER
setcapital 100000
order buy SPY 10 market
positions
balance

# Run a 4-year backtest
las_shell templates/backtest_runner.sh
```

Templates are auto-resolved from `$LAS_SHELL_HOME/templates/` (defaults to `/usr/local/share/las_shell/templates/`).

---

## Build

### Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| GCC | ≥ 9 | Compiler |
| GNU Make | any | Build system |
| libreadline | ≥ 7 | Interactive line editing |
| Python | ≥ 3.8 | Pipeline stages + scripts |
| libcurl | any | Live broker HTTP *(optional)* |
| OpenSSL | any | SHA-256 audit hashing *(optional)* |

```bash
# Ubuntu / Debian
sudo apt install build-essential libreadline-dev libssl-dev libcurl4-openssl-dev python3
```

### Build Commands

```bash
make                  # paper mode (no live broker)
make CURL=1           # with live broker HTTP support
make pipeline         # build C pipeline stage binaries
```

### Install (system-wide)

```bash
sudo make install     # installs to /usr/local/bin/las_shell
las_shell             # available from anywhere
las_quote AAPL        # price feed from anywhere
```

### Uninstall

```bash
sudo make uninstall
```

---

## Run

```bash
las_shell                              # interactive REPL
las_shell script.sh                    # run a strategy script
las_shell -c "echo $MARKET"           # single command
las_shell --audit strategy.sh          # audit mode — every command logged
make run-sim                           # shell + paper sim server on port 8080
```

---

## Project Layout

```
Las_shell/
├── src/                        # Shell core — C source files
│   ├── main.c                  # Entry point, REPL loop, startup/shutdown
│   ├── Commands.c              # All built-in commands
│   ├── operators.c             # @time, |>, ?>, ~> operator engine
│   ├── pipes.c                 # Unix pipe execution
│   ├── redirection.c           # I/O redirection + |> CSV operator
│   ├── substitution.c          # $() command substitution
│   ├── streaming_sub.c         # $<() streaming substitution (Phase 3)
│   ├── input_parser.c          # Tokeniser / lexer
│   ├── script.c                # Script execution engine
│   ├── alias.c                 # Alias engine
│   ├── history.c               # Readline history + signal handlers
│   ├── prompt.c                # Coloured trading-aware prompt
│   ├── helper.c                # String utilities
│   ├── trading_aliases_init.c  # Trading alias library loader (Phase 1)
│   ├── risk_config.c           # ~/.las_shell_risk config parser (Phase 4.3)
│   ├── audit.c                 # --audit mode + compliance log (Phase 4.1)
│   ├── broker.c                # Broker REST API bridge (Phase 4.2)
│   └── crash_recovery.c        # Checkpoint / crash recovery (Phase 4.4)
│
├── include/                    # C headers
│   ├── my_own_shell.h          # Master header — all types, externs, prototypes
│   ├── broker.h                # Broker API types and declarations
│   ├── risk_config.h           # RiskConfig struct and loader API
│   └── crash_recovery.h        # Checkpoint public API
│
├── pipeline/                   # Quant pipeline stages
│   ├── run_pipeline.sh         # End-to-end pipeline runner
│   ├── src/                    # C implementations (fast path)
│   │   ├── universe.c
│   │   ├── momentum_filter.c
│   │   ├── risk_filter.c
│   │   ├── size_positions.c
│   │   ├── execute.c
│   │   └── makefile
│   └── python/                 # Python implementations (flexible path)
│       ├── universe.py
│       ├── momentum_filter.py
│       ├── risk_filter.py
│       ├── size_positions.py
│       └── execute.py
│
├── scripts/                    # Python helper scripts
│   ├── pnl_report.py           # P&L reporter (alias: pnl)
│   ├── backtesting_harness.py  # Backtest engine (used by ~> operator)
│   ├── risk_check.py           # Pre-trade risk validator (used by ?> gate)
│   ├── quote.py                # Live price feed
│   ├── price_feed.py           # Streaming price source for $<()
│   ├── order_executor.py       # Order router
│   ├── market_maker.py         # Market-making signal generator
│   ├── momentum.py             # Momentum signal calculator
│   ├── pairs_spread.py         # Pairs trading spread calculator
│   ├── gen_momentum_orders.py  # Momentum order generator
│   ├── gen_pairs_orders.py     # Pairs order generator
│   ├── check_hedge_ratio.py    # Hedge ratio validator
│   ├── run_backtest.py         # Backtest orchestrator
│   ├── sim_server.py           # Paper trading simulation server
│   ├── market_daemon.sh        # Market status daemon (feeds prompt)
│   └── quote.sh                # Quote shell wrapper
│
├── templates/                  # Ready-to-run strategy templates
│   ├── momentum_daily.sh       # Daily momentum strategy
│   ├── momentum.sh             # Intraday momentum
│   ├── mean_reversion.sh       # Pairs / mean-reversion with assert guards
│   ├── market_make.sh          # Bid-ask spread capture with ?> risk gates
│   └── backtest_runner.sh      # Loops a strategy over multiple date ranges
│
├── strategies/                 # Strategy runtime scripts
│   ├── momentum_daily.sh
│   ├── momentum.sh
│   ├── mean_reversion.sh
│   ├── market_make.sh
│   ├── backtest_runner.sh
│   ├── streaming_strategy_demo.sh  # $<() streaming substitution demo
│   └── trading_aliases.sh          # Default trading alias library
│
├── tests/
│   ├── unit/                   # C unit tests (compiled and run directly)
│   │   ├── test_parser.c       # 14 assertions — streaming substitution parser
│   │   ├── test_risk_config.c  # 57 assertions — risk config validation
│   │   └── test_stream_sub_unit.c  # 15 assertions — FIFO lifecycle
│   └── integration/            # Shell-level integration tests
│       ├── tests.sh                        # Core shell: 152 tests
│       ├── test_audit.sh                   # Phase 4.1 audit log: 29 tests
│       ├── test_broker.sh                  # Phase 4.2 broker API: 59 tests
│       ├── test_pipeline.sh                # Phase 3 pipeline: 42 tests
│       ├── test_streaming_sub.sh           # Phase 3 $<() substitution: 5 tests
│       ├── test_templates.sh               # Phase 3 templates + scripts: 76 tests
│       ├── test_risk_config_integration.sh # Phase 4.3 risk config: 28 tests
│       └── test_crash_recovery.sh          # Phase 4.4 crash recovery: 26 tests
│
├── config/                     # Configuration templates
│   ├── trading_env.example     # Copy to ~/.trading_env
│   └── las_shell_risk.example  # Copy to ~/.las_shell_risk
│
├── docs/
│   └── PIPELINE_CONVENTION.md  # JSON stdin/stdout contract for pipeline stages
│
├── logs/                       # Runtime log output (auto-generated)
├── Makefile
├── .gitignore
└── README.md
```

---

## The 16 Features (4 Phases)

### Phase 1 — Trading Foundation
| Feature | Built-in | Description |
|---------|----------|-------------|
| Financial env vars | `setmarket` `setbroker` `setaccount` `setcapital` | Write to `~/.trading_env`, injected into every child process |
| Trading prompt | *(auto)* | Shows `[NYSE: OPEN +00:47] [P&L: +$1,240]` |
| Default alias library | *(auto-loaded on `setmarket`)* | `pnl`, `positions`, `flatten`, `backtest`, `eod` |
| assert built-in | `assert` | `assert $DRAWDOWN < 5.0` — abort script on risk breach |

### Phase 2 — New Operators
| Feature | Syntax | Description |
|---------|--------|-------------|
| @time scheduling | `@09:30:00 cmd` | Block until wall-clock time, then execute |
| \|> CSV logging | `cmd \|> file.csv` | Pipe stdout to CSV with ISO timestamp prefix |
| ?> risk gate | `orders ?> risk_check.py` | Forward only if risk check exits 0 |
| watch built-in | `watch 5 positions` | Repeat command every N seconds |

### Phase 3 — Strategy Pipeline
| Feature | Syntax | Description |
|---------|--------|-------------|
| $<() streaming | `price=$<(quote AAPL)` | Named-pipe backed streaming substitution |
| ~> backtest | `execute ~> 2020:2023` | Inject `BACKTEST_MODE=1`, route to harness |
| Pipeline convention | *(stdin/stdout JSON)* | Language-agnostic stage contract |
| Strategy templates | `templates/*.sh` | 5 ready-to-run strategy templates |

### Phase 4 — Audit & Production
| Feature | Flag / Built-in | Description |
|---------|-----------------|-------------|
| --audit mode | `--audit` | Append-only log: timestamp + user + cmd + exit + SHA-256 chain |
| Broker API bridge | `order` `positions` `balance` | IBKR / Alpaca / sim via libcurl |
| Risk limit config | *(auto-loaded)* | `~/.las_shell_risk` enforced by `?>` gate |
| Crash recovery | `checkpoint` | Periodic state save; restore on restart after crash |

---

## Configuration

The shell auto-detects install paths. `LAS_SHELL_HOME` falls back to `/usr/local/share/las_shell` if not set.

### `~/.trading_env`
```bash
# Copy from config/trading_env.example
MARKET=NYSE
BROKER=IBKR
ACCOUNT=PAPER      # PAPER | LIVE
CAPITAL=100000
BROKER_API=http://localhost:8080
```

### `~/.las_shell_risk`
```
# Copy from config/las_shell_risk.example
MAX_POSITION_SIZE   = 1000
MIN_POSITION_SIZE   = 1
MAX_DRAWDOWN_PCT    = 5.0
MAX_DAILY_LOSS      = 2000
MAX_ORDER_NOTIONAL  = 500000
ALLOWED_SYMBOLS     = SPY,QQQ,IWM,AAPL,MSFT
BLOCKED_SYMBOLS     = GME,AMC
```

---

## Tests

```bash
make test          # full suite: 508 tests (503 pass, 5 documented skips)
make test-unit     # 3 C unit tests (86 assertions)
make test-int      # 8 integration suites (417 tests)
```

| Suite | Tests | Result |
|-------|-------|--------|
| Core Shell | 152 | ✓ All pass |
| Audit Log | 29 | ✓ All pass |
| Broker API | 59 | ✓ All pass (5 documented skips) |
| Pipeline Convention | 42 | ✓ All pass |
| Streaming $<() | 5 | ✓ All pass |
| Templates + Scripts | 76 | ✓ All pass |
| Risk Config | 28 | ✓ All pass |
| Crash Recovery | 26 | ✓ All pass |
| Unit Tests (C) | 86 | ✓ All pass |

---

## The End State (from the roadmap)

```bash
#!/usr/bin/las_shell
setmarket NYSE
setbroker IBKR
setaccount LIVE
setcapital 500000

assert $DRAWDOWN < 3.0
assert $CAPITAL  > 100000

@09:30:00 universe SPY constituents            \
  | momentum_filter --lookback 20 --top 10    \
  | risk_filter --max-pos 1000                \
  | size_positions --capital $CAPITAL         \
  ?> risk_check.py                            \
  | execute |> trades.csv

watch 60 pnl |> daily_pnl.csv

@15:55:00 flatten
```

To backtest 4 years — change exactly one token:

```bash
  | execute ~> 2020-01-01:2023-12-31
```

---

## License

MIT © 2025 slimissa
```