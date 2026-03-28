# QShell — Quantitative Shell

> A Unix-grade shell with quant finance as a first-class citizen at every layer.  
> Built in C. Extensible via Python. Part of the [QuantOS](https://github.com/slimissa/LAS_Shell) platform.

---

## Overview

QShell is the command-line nucleus of **QuantOS** — an ambitious full-stack computing platform designed from scratch for quantitative finance workloads. Where standard shells treat numbers as strings and finance as an afterthought, QShell treats market data, signals, and order flows as native primitives.

```
qshell> @price AAPL |> momentum_filter ?> vol > 0.02 |> size_positions |> execute
```

---

## Features

### Shell Core
- Full REPL with readline-style history and line editing
- Tokenizer, lexer, and command dispatcher written in C
- Pipes (`|`), redirections (`>`, `>>`, `<`, `2>`), background jobs (`&`)
- Alias system with a built-in **trading alias library**
- Colored prompt with configurable context display
- Job control (`jobs`, `fg`, `bg`, `kill`)

### Quant Operators

| Operator | Syntax | Description |
|----------|--------|-------------|
| `\|>` | `cmd \|> filter` | Typed financial pipe — passes JSON signal objects between stages |
| `?>` | `cmd ?> expr` | Conditional filter — passes signals only if `expr` is true |
| `@time` | `@time cmd` | Execution timer — prints wall-clock time of any command |

### JSON Pipeline Convention

Every stage in a QShell pipeline speaks a common JSON dialect:

```json
{
  "symbol": "AAPL",
  "signal": 0.82,
  "size": 10,
  "price": 186.36,
  "side": "BUY",
  "meta": {
    "_convention": "1.0",
    "strategy": "momentum",
    "stage": "risk_filter",
    "timestamp": "2026-03-25T14:26:24"
  }
}
```

Stages are **composable**, **testable**, and **replaceable independently** — exactly like Unix pipes, but for trading signals.

### Strategy Templates

Four ready-to-use strategy templates ship with QShell:

| Template | Description |
|----------|-------------|
| `momentum` | Price momentum signal generator |
| `mean_reversion` | Z-score based mean reversion |
| `risk_filter` | Volatility-based position filter |
| `pairs_trade` | Correlation-based pair signal |

### C + Python Hybrid Pipeline

QShell pipelines can mix C stages (for speed-critical computation) with Python stages (for flexible logic) in a single command:

```
universe (C) → momentum_filter (Python) → risk_filter (C) → execute (Python)
```

Both sides emit and consume the same JSON convention — the language boundary is invisible to the pipeline.

---

## Architecture

```
LAS_Shell/
├── src/                  # Shell core (C)
│   ├── main.c            # Entry point, REPL loop
│   ├── tokenizer.c       # Lexer + tokenizer
│   ├── executor.c        # Command dispatch, pipes, redirections
│   ├── operators.c       # @time, |>, ?> implementations
│   ├── aliases.c         # Alias engine + trading library
│   └── jobs.c            # Job control
├── pipeline/             # Quant pipeline stages
│   ├── run_pipeline.sh   # Main pipeline runner
│   ├── universe.py       # Signal universe generator
│   ├── momentum_filter.py
│   ├── risk_filter.py
│   ├── size_positions.py
│   └── execute.py
├── templates/            # Strategy templates
│   ├── momentum.sh
│   ├── mean_reversion.sh
│   ├── risk_filter.sh
│   └── pairs_trade.sh
├── tests/                # Test suite (215 tests)
│   ├── shell_core/
│   ├── template_lib/
│   └── pipeline/
├── Makefile
└── README.md
```

---

## Build & Run

### Prerequisites

- GCC or Clang
- Python 3.8+
- GNU Make

### Build

```bash
git clone --branch qshell-dev https://github.com/slimissa/LAS_Shell.git
cd LAS_Shell
make
```

### Run

```bash
./las_shell              # Interactive REPL
./las_shell script.sh    # Execute a script
```

### Run the Pipeline

```bash
./las_shell pipeline/run_pipeline.sh
```

---

## Usage

### Basic Shell

```sh
qshell> ls -la
qshell> echo "hello" | tr a-z A-Z
qshell> sleep 5 &
[1] 12345
```

### Quant Operators

```sh
# Time any command
qshell> @time python3 pipeline/universe.py

# Pipe signals through filter stages
qshell> universe |> momentum_filter |> size_positions

# Conditional filter: only pass signals where vol > 0.02
qshell> universe |> risk_filter ?> vol > 0.02 |> execute
```

### Trading Aliases

```sh
qshell> price AAPL          # alias for: fetch_price --symbol AAPL
qshell> vol MSFT --days 30  # alias for: compute_vol --symbol MSFT --window 30
```

### Run a Strategy Template

```sh
qshell> ./templates/momentum.sh --capital 100000 --account PAPER
```

---

## Tests

```bash
make test                    # Run all 215 tests
make test-core               # Shell core tests only
make test-templates          # Template library tests only
make test-pipeline           # JSON pipeline tests only
```

Current test status: **215 passing** across all three suites.

---

## Roadmap

QShell is Phase 2 of the QuantOS build. The full roadmap:

| Phase | Component | Status |
|-------|-----------|--------|
| 1 | Shell nucleus (REPL, pipes, jobs, aliases) | ✅ Complete |
| 2 | Quant operators, pipeline, strategy templates | 🔄 In progress |
| 3 | Python bindings (`import qshell`) | 🔲 Planned |
| 4 | Quant built-ins (`@price`, `@vol`, `@greeks`) | 🔲 Planned |
| 5 | Plugin system | 🔲 Planned |
| 6 | QLang (custom scripting language) | 🔲 Planned |
| 7 | QDB (time-series database) | 🔲 Planned |
| 8 | Custom kernel & hardware drivers | 🔲 Long-term |

---

## Branch: `qshell-dev`

This branch is the active development front for **QShell Phase 2**. It contains:

- Operator implementations (`@time`, `|>`, `?>`)
- Trading alias library
- Four strategy templates
- JSON pipeline convention (v1.0)
- Full pipeline test suite

The `main` branch tracks stable shell nucleus releases only.

---

## License

MIT © [slimissa](https://github.com/slimissa)