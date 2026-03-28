#!/bin/bash
# QShell Market Daemon
# Writes NYSE market status to ~/.qshell_market every 5 seconds
# Format: "STATUS MINUTES_UNTIL_CHANGE"
# Run in background: ./market_daemon.sh &

STATUS_FILE="$HOME/.qshell_market"

while true; do
    # Current time in seconds since midnight (local time)
    NOW=$(date +%s)
    H=$(date +%H); M=$(date +%M); S=$(date +%S)
    NOW_SECS=$(( 10#$H * 3600 + 10#$M * 60 + 10#$S ))

    # NYSE schedule (Eastern Time — adjust TZ as needed)
    PRE_OPEN=$(( 4 * 3600 ))         # 04:00
    MARKET_OPEN=$(( 9 * 3600 + 30 * 60 ))   # 09:30
    MARKET_CLOSE=$(( 16 * 3600 ))    # 16:00
    AFTER_CLOSE=$(( 20 * 3600 ))     # 20:00

    if   [ $NOW_SECS -lt $PRE_OPEN ]; then
        STATUS="CLOSED"
        MINS=$(( ($PRE_OPEN - $NOW_SECS) / 60 ))
    elif [ $NOW_SECS -lt $MARKET_OPEN ]; then
        STATUS="PRE"
        MINS=$(( ($MARKET_OPEN - $NOW_SECS) / 60 ))
    elif [ $NOW_SECS -lt $MARKET_CLOSE ]; then
        STATUS="OPEN"
        MINS=$(( ($MARKET_CLOSE - $NOW_SECS) / 60 ))
    elif [ $NOW_SECS -lt $AFTER_CLOSE ]; then
        STATUS="AFTER"
        MINS=$(( ($AFTER_CLOSE - $NOW_SECS) / 60 ))
    else
        STATUS="CLOSED"
        # Minutes until 04:00 next day
        MINS=$(( (24*3600 - $NOW_SECS + $PRE_OPEN) / 60 ))
    fi

    echo "$STATUS $MINS" > "$STATUS_FILE"
    sleep 5
done