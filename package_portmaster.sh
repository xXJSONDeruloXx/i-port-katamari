#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

OUT="build/katamari-portmaster.zip"
STAGE="build/pkg-portmaster"

[ -x build/katamari ] \
    || { echo "build/katamari missing - run make first" >&2; exit 1; }
[ -f build/libs.armhf/MANIFEST.txt ] \
    || { echo "build/libs.armhf missing - run make libs first" >&2; exit 1; }
[ -d build/libs.armhf/licenses ] \
    || { echo "build/libs.armhf/licenses missing - run make libs again" >&2; exit 1; }

rm -rf "$STAGE"
rm -f "$OUT"
mkdir -p "$STAGE/katamari"

cp "ports/Katamari.sh"                      "$STAGE/"
cp build/katamari                             "$STAGE/katamari/"
cp ports/katamari/katamari.gptk               "$STAGE/katamari/"
cp ports/katamari/katamari.eapx.json          "$STAGE/katamari/"
cp ports/katamari/PUT_KATAMARI_DATA_HERE.txt "$STAGE/katamari/"
cp tools/eapx.py                              "$STAGE/katamari/"
cp ports/katamari/port.json                   "$STAGE/katamari/"
cp ports/katamari/gameinfo.xml                "$STAGE/katamari/"
cp ports/katamari/README.md                   "$STAGE/katamari/"
cp ports/katamari/CREDITS.md                  "$STAGE/katamari/"
cp -R build/libs.armhf                        "$STAGE/katamari/"

mkdir -p "$STAGE/katamari/licenses/libraries"
cp LICENSE "$STAGE/katamari/licenses/LICENSE-portmaster-port.txt"
cp NOTICE.md "$STAGE/katamari/licenses/NOTICE.md"
cp third_party/gmloader/LICENSE.md \
   "$STAGE/katamari/licenses/LICENSE-gmloader.md"
cp third_party/deadspace-vita/LICENSE \
   "$STAGE/katamari/licenses/LICENSE-deadspace-vita.txt"
cp third_party/powervr/LICENSE.md \
   "$STAGE/katamari/licenses/LICENSE-powervr.txt"
cp third_party/vfpvector/LICENSE \
   "$STAGE/katamari/licenses/LICENSE-vfpvector.txt"
mv "$STAGE/katamari/libs.armhf/licenses/"* \
   "$STAGE/katamari/licenses/libraries/"
rmdir "$STAGE/katamari/libs.armhf/licenses"

chmod +x "$STAGE/Katamari.sh" "$STAGE/katamari/katamari" \
         "$STAGE/katamari/eapx.py"
find "$STAGE" \( -name '._*' -o -name '.DS_Store' \) -delete
(cd "$STAGE" && zip -qr "../../$OUT" .)

EXPECTED_SIGNATURE="# PORTMASTER: katamari-portmaster.zip, Katamari.sh"
ACTUAL_SIGNATURE="$(unzip -p "$OUT" "Katamari.sh" | sed -n '2p')"
[ "$ACTUAL_SIGNATURE" = "$EXPECTED_SIGNATURE" ] || {
    echo "package has wrong PortMaster signature: $ACTUAL_SIGNATURE" >&2
    exit 1
}
[ "$(unzip -p "$OUT" "Katamari.sh" | wc -c | tr -d ' ')" -gt 0 ] || {
    echo "package has an empty launcher" >&2
    exit 1
}
unzip -tq "$OUT" >/dev/null

listing="$(unzip -Z1 "$OUT")"
for required in \
    "Katamari.sh" "katamari/katamari" "katamari/katamari.gptk" \
    "katamari/katamari.eapx.json" "katamari/eapx.py" \
    "katamari/PUT_KATAMARI_DATA_HERE.txt" "katamari/port.json" \
    "katamari/gameinfo.xml" "katamari/README.md" "katamari/CREDITS.md" \
    "katamari/licenses/LICENSE-portmaster-port.txt" \
    "katamari/licenses/LICENSE-gmloader.md" \
    "katamari/licenses/LICENSE-powervr.txt" \
    "katamari/licenses/libraries/libmpg123.so.0.copyright" \
    "katamari/libs.armhf/MANIFEST.txt" \
    "katamari/libs.armhf/libmpg123.so.0"; do
    case "$listing" in
        *"$required"*) ;;
        *) echo "package missing $required" >&2; exit 1 ;;
    esac
done

case "$listing" in
    *.apk|*assets/fat.bin|*lib/armeabi/libkatamari.so)
        echo "refusing package: proprietary game data found" >&2
        exit 1
        ;;
esac

built_sha="$(shasum -a 256 build/katamari | cut -d' ' -f1)"
packed_sha="$(unzip -p "$OUT" katamari/katamari | shasum -a 256 | cut -d' ' -f1)"
[ "$built_sha" = "$packed_sha" ] || {
    echo "refusing package: zipped binary differs from build/katamari" >&2
    exit 1
}

echo "$OUT"
echo "port version: $(sed -n 's/^#define KATAMARI_PORT_VERSION \"\(.*\)\"$/\1/p' src/port_version.h)"
echo "binary sha256: $built_sha"
du -h "$OUT"
