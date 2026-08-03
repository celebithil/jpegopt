#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-$HOME/jpeg_corpora}"
OUT="$ROOT/all"
mkdir -p "$OUT" "$ROOT/coco" "$ROOT/openimages/imgs" "$ROOT/mozjpeg" "$ROOT/flickr"

SUMS="$ROOT/.sums.txt"
TODO="$ROOT/.todo.txt"
SEEN="$OUT/.seen.hashes"
touch "$SEEN"
: > "$TODO"

echo "==> hashing all jpeg files"
find "$ROOT/coco" "$ROOT/openimages/imgs" "$ROOT/mozjpeg" "$ROOT/flickr" \
  -type f \( -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.jpe' \) -print0 \
  | xargs -0 sha256sum > "$SUMS"

echo "==> deduplicating by content"
awk -v seenfile="$SEEN" '
  BEGIN{ while((getline l < seenfile) > 0) seen[$1]=1 }
  { if (!seen[$1] && !keep[$1]++) print $1 "\t" substr($0,67) }
' "$SUMS" > "$TODO"

n="$(ls -1 "$OUT" 2>/dev/null | wc -l)"
copied=0
while IFS=$'\t' read -r h f; do
  src="misc"
  case "$f" in
    */coco/*)                 src="coco" ;;
    */openimages/imgs/*)      src="openimages" ;;
    */mozjpeg/*)              src="mozjpeg" ;;
    */flickr/*)               src="flickr" ;;
  esac
  n=$((n + 1))
  cp "$f" "$OUT/${src}_$(printf '%05d' "$n")__$(basename "$f")"
  echo "$h" >> "$SEEN"
  copied=$((copied + 1))
done < "$TODO"

rm -f "$SUMS" "$TODO"

total="$(ls -1 "$OUT" 2>/dev/null | wc -l)"
echo "==> flattened: $copied new files, $total total in $OUT"
for src in coco openimages mozjpeg flickr misc; do
  c="$(ls -1 "$OUT"/"$src"_* 2>/dev/null | wc -l || true)"
  [ "$c" -gt 0 ] && echo "    $src: $c"
done
