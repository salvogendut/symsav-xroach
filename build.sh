#!/bin/bash
# Build xroach for SymbOS using scc

SCC="${SCC:-../scc/bin/cc}"

"$SCC" xroach.c \
    -N "XRoach" \
    -o xroach.sav \
    -h 512 \
    -lgfx

python3 add_preview.py
