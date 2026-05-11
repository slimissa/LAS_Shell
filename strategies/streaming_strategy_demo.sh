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
# ─────────────────────────────────────────────────────────────────────────

echo "========================================================"
echo " Las_shell $<() — Intraday Momentum Streaming Demo"
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
    echo "  tick → $SYMBOL = \$$price"

    # Risk guard: price must be positive
    assert $price > 0

    # Signal: if price > threshold, send a simulated order
    if [ $(echo "$price > $THRESHOLD" | bc -l) -eq 1 ]; then
        echo "    [SIGNAL] price \$$price > \$$THRESHOLD — BUY 100 $SYMBOL"
        echo "BUY,$SYMBOL,100,$price,$(date +%H:%M:%S)" \
            ?> python3 ../scripts/risk_check.py --max_notional 100000 --max_size 1000 \
            |> /tmp/streaming_demo_trades.csv \
            && echo "    [RISK] order passed" \
            || echo "    [RISK] order rejected"
        setenv ORDERS_SENT $(expr $ORDERS_SENT + 1)
    fi
done

echo ""
echo "--- Stream exhausted ---"
echo "Orders sent : $ORDERS_SENT"
echo "Trade log   : /tmp/streaming_demo_trades.csv"
echo ""
echo "Done."
