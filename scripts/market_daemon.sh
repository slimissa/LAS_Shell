#!/bin/bash
# Las_shell Market Daemon
# Writes NYSE market status to ~/.las_shell_market every 5 seconds
# Format: "STATUS MINUTES_UNTIL_CHANGE"
# Run in background: ./market_daemon.sh &

STATUS_FILE="$HOME/.las_shell_market"
export TZ=America/New_York

while true; do
    # Current time in seconds since midnight (Eastern Time)
    DOW=$(date +%u)  # 1=Monday ... 7=Sunday
    H=$(date +%H); M=$(date +%M); S=$(date +%S)
    NOW_SECS=$(( 10#$H * 3600 + 10#$M * 60 + 10#$S ))

    # Weekends: market closed until Monday 04:00 ET
    if [ $DOW -ge 6 ]; then
        echo "CLOSED $(( (24*3600 - $NOW_SECS + (8 - DOW) * 24 * 3600 + 4*3600) / 60 ))" > "$STATUS_FILE"
        sleep 5
        continue
    fi

    # NYSE schedule (Eastern Time)
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