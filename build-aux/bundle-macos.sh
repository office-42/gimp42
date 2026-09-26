#!/usr/bin/env bash
# bundle-macos.sh - make a GIMP42.app from a Homebrew build, on macOS.
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     meson setup builddir --prefix="$PWD/dist" && meson install -C builddir
#     build-aux/bundle-macos.sh dist GIMP42.app
#
# The app holds the installed tree (program, plug-ins, data), every dylib
# they need (found and rewritten by dylibbundler), GTK's schemas, the
# pixbuf loaders, the icons GTK asks for, and a launcher that points GLib,
# GDK and the GIMP at them.  It is not signed or notarised: Gatekeeper will
# want a right-click Open the first time.

set -uo pipefail
[ -n "${GIMP42_BUNDLE_VERBOSE:-}" ] && set -x

dist=${1:-dist}
app=${2:-GIMP42.app}
prefix=$(brew --prefix)

contents="$app/Contents"
res="$contents/Resources"
rm -rf "$app"
mkdir -p "$contents/MacOS" "$contents/Frameworks" "$res"

[ -f "$dist/bin/gimp42" ] || { echo "no gimp42 in $dist/bin; run meson install first" >&2; exit 1; }

# The installed tree goes into Resources as it is.
cp -R "$dist/bin" "$dist/lib" "$dist/share" "$res/"
mv "$res/bin/gimp42" "$contents/MacOS/gimp42-bin"

# --- the launcher --------------------------------------------------------
cat > "$contents/MacOS/gimp42" <<'EOF'
#!/bin/bash
here="$(cd "$(dirname "$0")" && pwd)"
res="$(cd "$here/../Resources" && pwd)"
export GSETTINGS_SCHEMA_DIR="$res/share/glib-2.0/schemas"
export XDG_DATA_DIRS="$res/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export GIMP42_DATADIR="$(echo "$res"/share/gimp42/*)"
export GIMP42_PLUGINDIR="$(echo "$res"/lib/gimp42/*)"
if [ -f "$res/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache.in" ]; then
  cache="${TMPDIR:-/tmp}/gimp42-loaders-$$.cache"
  sed "s|@RES@|$res|g" "$res/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache.in" > "$cache"
  export GDK_PIXBUF_MODULE_FILE="$cache"
fi
exec "$here/gimp42-bin" "$@"
EOF
chmod +x "$contents/MacOS/gimp42"

# --- resources ------------------------------------------------------------
mkdir -p "$res/share/glib-2.0/schemas"
cp "$prefix"/share/glib-2.0/schemas/org.gtk.gtk4.Settings.*.xml "$res/share/glib-2.0/schemas/" 2>/dev/null || true
glib-compile-schemas "$res/share/glib-2.0/schemas" || true

loaders="$res/lib/gdk-pixbuf-2.0/2.10.0/loaders"
mkdir -p "$loaders"
for l in png jpeg gif bmp ico xpm svg; do
  for f in "$prefix"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*"$l"*.so; do
    [ -f "$f" ] && cp "$f" "$loaders/"
  done
done

mkdir -p "$res/share/icons"
cp -R "$prefix/share/icons/hicolor" "$res/share/icons/" 2>/dev/null || true
if [ -d "$prefix/share/icons/Adwaita" ]; then
  mkdir -p "$res/share/icons/Adwaita"
  cp "$prefix/share/icons/Adwaita/index.theme" "$res/share/icons/Adwaita/" 2>/dev/null || true
  for sub in scalable symbolic 16x16 24x24 32x32; do
    [ -d "$prefix/share/icons/Adwaita/$sub" ] && cp -R "$prefix/share/icons/Adwaita/$sub" "$res/share/icons/Adwaita/"
  done
  gtk4-update-icon-cache -q -t -f "$res/share/icons/Adwaita" 2>/dev/null || true
fi

version=$(sed -n "s/^  version: '\([^']*\)',/\1/p" meson.build | head -1)
cat > "$contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>GIMP42</string>
  <key>CFBundleDisplayName</key><string>GIMP42</string>
  <key>CFBundleIdentifier</key><string>org.gimp42.gimp42</string>
  <key>CFBundleVersion</key><string>${version:-0.0.0}</string>
  <key>CFBundleShortVersionString</key><string>${version:-0.0.0}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>gimp42</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

# --- the libraries ---------------------------------------------------------
# dylibbundler copies every Homebrew dylib the program, the plug-ins and
# the loaders link to into Frameworks and rewrites their install names.
targets=("$contents/MacOS/gimp42-bin")
while IFS= read -r -d '' f; do
  targets+=("$f")
done < <(find "$res/lib" -type f \( -perm -u+x -o -name '*.so' \) -print0)
args=()
for t in "${targets[@]}"; do args+=(-x "$t"); done
dylibbundler -od -b -d "$contents/Frameworks" -p @executable_path/../Frameworks \
  -s "$prefix/lib" "${args[@]}" > /dev/null || { echo "dylibbundler failed" >&2; exit 1; }

# The plug-ins run from Resources/lib/..., not MacOS; point them at the
# same Frameworks folder through an rpath relative to themselves.
while IFS= read -r -d '' f; do
  rel=$(python3 -c "import os,sys; print(os.path.relpath(sys.argv[1], os.path.dirname(sys.argv[2])))" "$contents/Frameworks" "$f")
  install_name_tool -add_rpath "@loader_path/$rel" "$f" 2>/dev/null || true
  for lib in $(otool -L "$f" | awk '/@executable_path\/..\/Frameworks/ {print $1}'); do
    install_name_tool -change "$lib" "@rpath/$(basename "$lib")" "$f" 2>/dev/null || true
  done
done < <(find "$res/lib" -type f -perm -u+x -print0)

# --- the check ----------------------------------------------------------------
bad=0
for f in "$contents/MacOS/gimp42-bin" "$contents/Frameworks"/*.dylib; do
  [ -f "$f" ] || continue
  if otool -L "$f" | grep -q "$prefix"; then
    echo "still linked to $prefix: $f" >&2
    bad=1
  fi
done
[ "$bad" -eq 0 ] || exit 1

( cd "$res" && GDK_PIXBUF_MODULEDIR="$loaders" gdk-pixbuf-query-loaders "$loaders"/*.so 2>/dev/null ) \
  | sed "s|$(cd "$res" && pwd)|@RES@|g" > "$res/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache.in" || true

echo "bundled $(ls "$contents/Frameworks" | wc -l | tr -d ' ') libraries into $app"
