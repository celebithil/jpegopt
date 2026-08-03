#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-$HOME/jpeg_corpora}"
mkdir -p "$ROOT"/{coco,openimages,mozjpeg,flickr}

echo "==> corpora root: $ROOT"

fetch_url() {
  local url="$1" outdir="$2" file="$3"
  local s3_url="https://s3.us-east-1.amazonaws.com/images.cocodataset.org/zips/$file"
  local https_url="$(printf '%s' "$url" | sed 's#^http://#https://#')"
  local ua="Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0"
  local aria=("--user-agent=$ua" "--max-tries=0" "--retry-wait=5"
              "--file-allocation=falloc" "-c" "-k1M")
  if command -v aria2c >/dev/null 2>&1; then
    echo "    try s3-rest https..."
    aria2c "${aria[@]}" -x4 -s4 -d "$outdir" -o "$file" "$s3_url" \
    || { echo "    s3 failed, try direct https...";
         aria2c "${aria[@]}" -x4 -s4 -d "$outdir" -o "$file" "$https_url"; } \
    || { echo "    https failed, try ipv6...";
         aria2c "${aria[@]}" --disable-ipv4 -x2 -s2 -d "$outdir" -o "$file" "$url"; } \
    || { echo "    ipv6 failed, try ipv4...";
         aria2c "${aria[@]}" -x4 -s4 -d "$outdir" -o "$file" "$url"; }
  else
    wget -c -O "$outdir/$file" "$url"
  fi
}

echo "==> [1/4] COCO 2017 (train2017 + val2017 + test2017)"
for z in train2017 val2017 test2017; do
  if [ ! -d "$ROOT/coco/$z" ]; then
    if [ ! -f "$ROOT/coco/$z.zip" ] || ! unzip -t -q "$ROOT/coco/$z.zip" 2>/dev/null; then
      echo "    downloading $z.zip"
      fetch_url "http://images.cocodataset.org/zips/$z.zip" "$ROOT/coco" "$z.zip"
    fi
    echo "    unzipping $z.zip"
    unzip -o -q "$ROOT/coco/$z.zip" -d "$ROOT/coco"
  fi
done

echo "==> [2/4] Open Images v7 (validation)"
if [ ! -f "$ROOT/openimages/ids.txt" ]; then
  if [ ! -f "$ROOT/openimages/validation-images-with-rotation.csv" ]; then
    wget -O "$ROOT/openimages/validation-images-with-rotation.csv" \
      https://storage.googleapis.com/openimages/2018_04/validation/validation-images-with-rotation.csv
  fi
  tail -n +2 "$ROOT/openimages/validation-images-with-rotation.csv" \
    | cut -d, -f1 \
    | sed 's/^/validation\//' \
    > "$ROOT/openimages/ids.txt"
  echo "    $(wc -l < "$ROOT/openimages/ids.txt") image ids"
fi
sed 's#^#https://open-images-dataset.s3.us-east-1.amazonaws.com/#' \
  "$ROOT/openimages/ids.txt" | sed 's#$#.jpg#' > "$ROOT/openimages/urls.txt"
expected="$(wc -l < "$ROOT/openimages/urls.txt")"
have="$(find "$ROOT/openimages/imgs" -name '*.jpg' 2>/dev/null | wc -l || true)"
if [ "$have" -lt $((expected - 50)) ]; then
  echo "    downloading $expected images via aria2c (s3-rest unsigned, no boto3)"
  mkdir -p "$ROOT/openimages/imgs"
  aria2c --user-agent="Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0" \
    --max-tries=0 --retry-wait=5 --file-allocation=falloc \
    --allow-overwrite=false --auto-file-renaming=false -c -k1M \
    -x1 -s1 -j"${OID_PROC:-16}" -d "$ROOT/openimages/imgs" -i "$ROOT/openimages/urls.txt" \
    || echo "    some downloads failed; rerun to resume"
else
  echo "    already have $have / $expected images, skipping"
fi

echo "==> [3/4] mozjpeg test images"
MOZ=https://raw.githubusercontent.com/mozilla/mozjpeg/master/testimages
for f in testorig.jpg testorig12.jpg testimgint.jpg testimgari.jpg; do
  [ -f "$ROOT/mozjpeg/$f" ] || wget -c -P "$ROOT/mozjpeg" "$MOZ/$f"
done

echo "==> [4/4] Flickr (requires FLICKR_KEY)"
if [ -n "${FLICKR_KEY:-}" ]; then
  FLICKR_KEY="$FLICKR_KEY" OUT="$ROOT/flickr" python3 - <<'PY'
import os, json, urllib.request, urllib.parse, time
key = os.environ["FLICKR_KEY"]
out = os.environ["OUT"]
os.makedirs(out, exist_ok=True)
n = 0
for page in range(1, 11):
    q = urllib.parse.urlencode({
        "method": "flickr.photos.search",
        "api_key": key,
        "format": "json",
        "nojsoncallback": 1,
        "per_page": 500,
        "page": page,
        "license": "1,2,3,4,5,6,7",
        "content_type": 1,
        "safe_search": 1,
    })
    with urllib.request.urlopen("https://api.flickr.com/services/rest/?" + q, timeout=30) as r:
        data = json.load(r)
    for p in data.get("photos", {}).get("photo", []):
        url = "https://live.staticflickr.com/%s/%s_%s_b.jpg" % (p["server"], p["id"], p["secret"])
        fn = os.path.join(out, p["id"] + ".jpg")
        try:
            urllib.request.urlretrieve(url, fn)
            if os.path.getsize(fn) > 0:
                n += 1
        except Exception:
            pass
    time.sleep(1)
print("flickr images:", n)
PY
else
  echo "    FLICKR_KEY not set, skipping"
fi

echo "==> done. now run: scripts/flatten_corpus.sh $ROOT"
