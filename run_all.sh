#!/bin/bash
LOGDIR="logs"
EMU="./emulator.exe"
GAMESDIR="games"
TIMEOUT=20

rm -f "$LOGDIR"/*.log

find "$GAMESDIR" -name '*.app' -print0 | while IFS= read -r -d '' app; do
    base="$(basename "$app" .app)"
    logfile="$LOGDIR/${base}.log"
    echo "=== Running: $base ==="
    SDL_VIDEODRIVER=dummy timeout $TIMEOUT "$EMU" "$app" > "$logfile" 2>&1
    ec=$?
    echo "EXIT_CODE=$ec" >> "$logfile"
    if [ $ec -eq 124 ]; then
        echo "STATUS=TIMEOUT" >> "$logfile"
        echo "  -> TIMEOUT"
    elif [ $ec -ne 0 ]; then
        echo "STATUS=CRASH" >> "$logfile"
        echo "  -> CRASH (exit=$ec)"
    else
        echo "STATUS=OK" >> "$logfile"
        echo "  -> OK"
    fi
done
