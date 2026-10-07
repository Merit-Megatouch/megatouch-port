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
# libmvideo (Merit3D video) does not list its codecs: load them globally before it
case " $libs " in *" libmvideo.so "*)
  for l in libtheora.so.0; do [ -s "$R/games/$g/lib/$l" ] || [ -e "$R/shared/runtime/$l" ] || "$R/scripts/lib/extract-libs.sh" "$R/games/$g/lib" $l 2>/dev/null; done
  for f in "$R/games/$g"/lib/*; do [ -e "$R/shared/runtime/$(basename "$f")" ] && rm -f "$f"; done
  libs=$(echo " $libs " | sed 's/ libmvideo.so / libogg.so.0 libvorbis.so.0 libtheora.so.0 libmvideo.so /' | tr -s ' ' | sed 's/^ //; s/ $//') ;;
esac
# Merit3D/AllegroGL games keep the GL libraries first, and always have libdebug_mock
gl=""; grep -q '^PRELOAD=.*libGL.so.1' "$c" && gl="libGL.so.1 libGLU.so.1 "
if [ -n "$gl" ]; then case " $libs " in *" libdebug_mock.so "*) ;; *) libs="libdebug_mock.so $libs";; esac
  [ -s "$R/games/$g/lib/libdebug_mock.so" ] || "$R/scripts/lib/extract-libs.sh" "$R/games/$g/lib" libdebug_mock.so 2>/dev/null; fi
line="PRELOAD=${gl}libmerit_legacy.so ${libs}${gendef}${stubs}"; line=$(echo "$line" | tr -s ' ' | sed 's/ $//')
if grep -q '^PRELOAD=' "$c"; then sed -i "s|^PRELOAD=.*|$line|" "$c"; else echo "$line" >> "$c"; fi
echo "$g: $line"
