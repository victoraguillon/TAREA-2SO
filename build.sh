#!/usr/bin/env bash

set -euo pipefail
ORIGEN="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DESTINO="${HOME}/laboratorio-build"

mkdir -p "$DESTINO"

if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete \
          --exclude 'build/' --exclude 'resultados/' \
          "$ORIGEN/" "$DESTINO/"
else

    rm -rf "$DESTINO/build"
    mkdir -p "$DESTINO"
    cp -a "$ORIGEN/." "$DESTINO/"
    rm -rf "$DESTINO/build" "$DESTINO/resultados"
fi

cmake -S "$DESTINO" -B "$DESTINO/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$DESTINO/build" -j"$(nproc)"

mkdir -p "$ORIGEN/build"
cp "$DESTINO/build/laboratorio" "$ORIGEN/build/laboratorio"
echo "Binario en $ORIGEN/build/laboratorio"
