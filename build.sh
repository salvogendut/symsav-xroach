#!/bin/bash
# Build xroach for SymbOS using scc

SCC="${SCC:-../scc/bin/cc}"

"$SCC" xroach.c \
    -N "XRoach" \
    -o xroach.com \
    -h 512 \
    -lgfx
