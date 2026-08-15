#!/usr/bin/las_shell
# streaming_strategy_demo.sh
# ─────────────────────────────────────────────────────────────────────────
# Demonstrates the complete $<() streaming pattern from the roadmap:
#
#   while true; do
#       price=$<(quote AAPL)
#       assert $price > 0
#       [ $price -gt 490 ] && trigger_order.py buy AAPL 100
#       sleep 1
#   done
#
# This demo uses 5 ticks from the quote simulator (--stream 5)
# so it terminates naturally without requiring Ctrl+C.
#
# FIX SD4: orders were previously risk-gated and logged but never
# actually executed -- there was no call to order_executor.py (or any
# execution step) anywhere in this script, so "orders sent" was really
# just "risk checks performed." Route through order_executor.py so a
# PASS actually places the (paper) order, and the trade log reflects
# real fills instead of just risk-check verdicts.
# ─────────────────────────────────────────────────────────────────────────

echo "========================================================"
echo " Las_shell \$<() — Intraday Momentum Streaming Demo"
echo "========================================================"
echo ""

setenv CAPITAL 500000
setenv SYMBOL AAPL
setenv THRESHOLD 100
setenv ORDERS_SENT 0

echo "Symbol    : $SYMBOL"
echo "Capital   : $CAPITAL"
echo "Threshold : Buy when price > $THRESHOLD"
echo ""
echo "--- Streaming 5 ticks ---"

while price=$<(./quote --stream 5 $SYMBOL)
do
    echo "  tick -> $SYMBOL = $price"

    # Risk guard: price must be positive
    assert $price > 0

    # Signal: if price > threshold, send an order through the full
    # gate -> execute -> log chain.
    if [ $(echo "$price > $THRESHOLD" | bc -l) -eq 1 ]; then
        echo "    [SIGNAL] price $price > $THRESHOLD -- BUY 100 $SYMBOL"
        echo "$SYMBOL BUY 100 $price" ?> python3 $LAS_SHELL_HOME/scripts/risk_check.py --max_notional 100000 --max_size 1000 | python3 $LAS_SHELL_HOME/scripts/order_executor.py --mode paper |> /tmp/streaming_demo_trades.csv
        setenv ORDERS_SENT $(expr $ORDERS_SENT + 1)
    fi
done

echo ""
echo "--- Stream exhausted ---"
echo "Orders sent : $ORDERS_SENT"
echo "Trade log   : /tmp/streaming_demo_trades.csv"
echo ""
echo "Done."
