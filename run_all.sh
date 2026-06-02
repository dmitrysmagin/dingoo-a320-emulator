#!/bin/bash
LOGDIR="logs"
EMU="./emulator.exe"
GAMESDIR="games"
TIMEOUT=120

rm -f "$LOGDIR"/*.log

find "$GAMESDIR" -name '*.app' -print0 | while IFS= read -r -d '' app; do
    base="$(basename "$app" .app)"
    logfile="$LOGDIR/${base}.log"
    echo "=== Running: $base ==="
    SDL_VIDEODRIVER=dummy timeout $TIMEOUT "$EMU" "$app" > "$logfile" 2>&1
    ec=$?
    echo "Exit code: $ec" >> "$logfile"
    if [ $ec -eq 124 ]; then
        echo "STATUS: TIMEOUT (2 min)" >> "$logfile"
        echo "  -> TIMEOUT"
    elif [ $ec -ne 0 ]; then
        echo "STATUS: CRASH/ERROR (exit=$ec)" >> "$logfile"
        echo "  -> CRASH (exit=$ec)"
    else
        # Check if it exited normally but early
        lastline=$(tail -1 "$logfile" | grep -oE 'STATUS:.*' || true)
        echo "  -> OK (exit=0)"
    fi
done
