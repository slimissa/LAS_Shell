#!/usr/bin/env bash
# Momentum strategy template
CAPITAL=${1:-100000}
ACCOUNT=${2:-PAPER}
python3 pipeline/universe.py | python3 pipeline/momentum_filter.py | python3 pipeline/risk_filter.py | python3 pipeline/size_positions.py | python3 pipeline/execute.py
