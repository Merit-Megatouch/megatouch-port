#!/bin/bash
# Set a legacy game's PRELOAD to libmerit_legacy.so + the cabinet service libraries it needs (in
# dependency order, extracted into games/<g>/lib) + libmega_stubs.so if the game uses stubs.
#   scripts/lib/legacy-preload.sh <game>
set -uo pipefail
R=$(cd "$(dirname "$0")/../.." && pwd)
g=$1; c="$R/games/$g/game.conf"
[ -s "$R/build/index/cabinet-syms.tsv" ] || "$R/tools/cabinet-providers.py" --index
libs=$("$R/tools/cabinet-providers.py" "$g" | tr '\n' ' ')
# the loader's gendef classes (src/gendef): needed when the game imports any of them; they sit on
# the cabinet's libgendef_xml/libgendef_common
gendef=""
if [ -f "$R/shared/bin/libmerit_gendef.so" ]; then
  defs=$(nm -D --defined-only "$R/shared/bin/libmerit_gendef.so" | awk '{print $3}' | grep -E 'xml_gamerandom|RandomizedArray|TriviaClass|DBFClass|ChainRest|TextUtils' | sort -u)
  imps=$(for so in "$R/games/$g"/lib/*.so; do [ -L "$so" ] || nm -D --undefined-only "$so" 2>/dev/null; done | awk '{print $2}' | sort -u)
  if [ -n "$(comm -12 <(echo "$defs") <(echo "$imps"))" ]; then
    gendef=" libmerit_gendef.so"
    for l in libdebug_mock.so libdebug_shared.so libgendef_common.so libgendef_xml.so; do
      case " $libs " in *" $l "*) ;; *) libs="$libs $l";; esac
    done
    ln -sfn ../../../shared/bin/libmerit_gendef.so "$R/games/$g/lib/libmerit_gendef.so"
  fi
fi
for l in $libs; do [ -s "$R/games/$g/lib/$l" ] || "$R/scripts/lib/extract-libs.sh" "$R/games/$g/lib" "$l" 2>/dev/null; done
for f in "$R/games/$g"/lib/*; do [ -e "$R/shared/runtime/$(basename "$f")" ] && rm -f "$f"; done
# libcontent reads the shared content packs (/usr/local/ion_only/content)
case " $libs " in *" libcontent.so "*)
  mkdir -p "$R/games/$g/data/usr/local/ion_only"
  [ -e "$R/games/$g/data/usr/local/ion_only/content" ] || \
    ln -sfn ../../../../../../shared/data-common/usr/local/ion_only/content "$R/games/$g/data/usr/local/ion_only/content" ;;
esac
stubs=""; grep -q '^PRELOAD=.*libmega_stubs.so' "$c" && stubs=" libmega_stubs.so"
line="PRELOAD=libmerit_legacy.so ${libs}${gendef}${stubs}"; line=$(echo "$line" | tr -s ' ' | sed 's/ $//')
if grep -q '^PRELOAD=' "$c"; then sed -i "s|^PRELOAD=.*|$line|" "$c"; else echo "$line" >> "$c"; fi
echo "$g: $line"
