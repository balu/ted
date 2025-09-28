#!/usr/bin/env sh
file="$1"
rows="$2"

grep -b '.*' "$file" \
    | fzf \
          -d: --with-nth=2.. \
          --multi --height="$rows" \
          --tac --no-sort --bind 'start:last' \
    | cut -d: -f1
