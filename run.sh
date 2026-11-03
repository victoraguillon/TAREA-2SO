#!/usr/bin/env bash

set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ ! -x "$DIR/build/laboratorio" ]; then
    "$DIR/build.sh"
fi

exec "$DIR/build/laboratorio" --puerto 8080 --abrir "$@"
