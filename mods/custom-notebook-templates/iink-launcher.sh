#!/bin/sh

binary=/usr/local/Kobo/iink

if [ "$1" = "exporter" ]; then
    quality=best
    marker=/mnt/onboard/.kobo/iink/custom-notebook-templates/export-quality
    if [ -r "$marker" ]; then
        IFS= read -r requested < "$marker"
        rm -f "$marker"
        case "$requested" in
            fast|best) quality=$requested ;;
        esac
    fi

    candidate=/usr/local/Kobo/iink-export-$quality
    if [ -x "$candidate" ]; then
        binary=$candidate
    fi
fi

LD_LIBRARY_PATH=/usr/local/Kobo LC_ALL=C \
    exec "$binary" -skipFontLoad -platform kobo:noscreen "$@"
