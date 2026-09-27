#!/usr/bin/env bash
# bundle-windows.sh - make an installed gimp42 tree runnable on a Windows
# machine without MSYS2, from an MSYS2 UCRT64 (or MINGW64) shell.
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     meson setup builddir --prefix="$(cygpath -m "$PWD/dist")"
#     meson install -C builddir
#     build-aux/bundle-windows.sh dist
#
# adds to dist/ what the program and its plug-ins need besides themselves:
# the DLLs (from ldd, followed to a fixed point, for gimp42.exe and every
# plug-in), GDK's pixbuf loaders, GTK's schemas and icons.  The program
# finds its data and plug-ins relative to dist/bin, so dist can be moved,
# zipped or installed anywhere.

# No -e: an optional file missing from the image must not stop the bundle.
set -uo pipefail

dist=${1:-dist}
prefix=${MINGW_PREFIX:-/ucrt64}

[ -f "$dist/bin/gimp42.exe" ] || { echo "no gimp42.exe in $dist/bin; run meson install first" >&2; exit 1; }

# The GDK pixbuf loaders for the everyday formats (GTK uses them for its
# own icons; the GIMP reads images through its plug-ins, and its pixbuf
# plug-in opens what the loaders read that no other plug-in does: SVG,
# Mac icons, animated cursors, X bitmaps, Windows metafiles...), and a
# cache naming just those, with paths relative to the bundle.
loaders="$dist/lib/gdk-pixbuf-2.0/2.10.0/loaders"
mkdir -p "$loaders"
for l in png jpeg gif bmp ico xpm ani icns qtif xbm tga pnm 'gdip-*'; do
  for f in "$prefix"/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-$l.dll; do
    [ -f "$f" ] && cp "$f" "$loaders/"
  done
done
[ -f "$prefix/lib/gdk-pixbuf-2.0/2.10.0/loaders/pixbufloader_svg.dll" ] &&
  cp "$prefix/lib/gdk-pixbuf-2.0/2.10.0/loaders/pixbufloader_svg.dll" "$loaders/"
( cd "$dist" && gdk-pixbuf-query-loaders lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll ) \
  | sed 's#^"[^"]*/lib/gdk-pixbuf-2.0/#"lib/gdk-pixbuf-2.0/#' > "$loaders/../loaders.cache" || true

# libheif's codec plug-ins, when it has them as separate DLLs (the GIMP
# points LIBHEIF_PLUGIN_PATH here); their DLLs are found below.
if [ -d "$prefix/lib/libheif" ]; then
  mkdir -p "$dist/lib"
  cp -r "$prefix/lib/libheif" "$dist/lib/"
fi

# GLib schemas (GTK needs its own), compiled.
mkdir -p "$dist/share/glib-2.0/schemas"
cp "$prefix"/share/glib-2.0/schemas/org.gtk.gtk4.Settings.*.xml "$dist/share/glib-2.0/schemas/" 2>/dev/null || true
cp "$prefix"/share/glib-2.0/schemas/gschema.dtd "$dist/share/glib-2.0/schemas/" 2>/dev/null || true
glib-compile-schemas "$dist/share/glib-2.0/schemas" || true

# Icons GTK's own widgets ask for (dialogs, menus, file chooser).
mkdir -p "$dist/share/icons"
cp -r "$prefix/share/icons/hicolor" "$dist/share/icons/" 2>/dev/null || true
if [ -d "$prefix/share/icons/Adwaita" ]; then
  mkdir -p "$dist/share/icons/Adwaita"
  cp "$prefix/share/icons/Adwaita/index.theme" "$dist/share/icons/Adwaita/" 2>/dev/null || true
  for sub in scalable symbolic 16x16 24x24 32x32; do
    [ -d "$prefix/share/icons/Adwaita/$sub" ] && cp -r "$prefix/share/icons/Adwaita/$sub" "$dist/share/icons/Adwaita/" || true
  done
fi
gtk4-update-icon-cache -q -t -f "$dist/share/icons/Adwaita" 2>/dev/null || true

# Fontconfig and the GTK settings that make it look at home on Windows.
[ -d "$prefix/etc/fonts" ] && { mkdir -p "$dist/etc"; cp -r "$prefix/etc/fonts" "$dist/etc/"; } || true
mkdir -p "$dist/etc/gtk-4.0"
cat > "$dist/etc/gtk-4.0/settings.ini" <<'EOF'
[Settings]
gtk-font-name=Segoe UI 9
EOF

# The DLLs: everything ldd finds under the prefix, for every program and
# every DLL copied so far, until nothing new turns up.  They all go next
# to gimp42.exe; the plug-ins find them because the GIMP puts its bin
# folder on PATH before it starts them (see below).
copy_deps () {
  local target=$1
  ldd "$target" 2>/dev/null | awk '{print $3}' | grep -i "^$prefix/" | while read -r dll; do
    local base
    base=$(basename "$dll")
    if [ ! -f "$dist/bin/$base" ]; then
      cp "$dll" "$dist/bin/"
      echo "$dist/bin/$base"
    fi
  done
}

queue=("$dist/bin/gimp42.exe")
while IFS= read -r -d '' f; do
  queue+=("$f")
done < <(find "$dist/lib" -name '*.exe' -print0 -o -name '*.dll' -print0)

while [ ${#queue[@]} -gt 0 ]; do
  next=()
  for f in "${queue[@]}"; do
    while read -r added; do
      [ -n "$added" ] && next+=("$added")
    done < <(copy_deps "$f")
  done
  queue=("${next[@]}")
done

# Windows looks for a program's DLLs next to the program first; the
# plug-ins live in lib/gimp42/<version>/plug-ins, so each gets a copy of
# the ones it needs there would bloat the bundle.  Instead the GIMP adds
# its bin folder to PATH for its plug-ins (app/plug_in.c).
echo "bundled $(ls "$dist/bin" | wc -l) files into $dist/bin"
[ -f "$dist/bin/libgtk-4-1.dll" ] || { echo "GTK's DLL was not found by ldd; the bundle would not run" >&2; exit 1; }
