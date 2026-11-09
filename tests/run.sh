#!/bin/bash

run_dir() {
    local titulo="$1"; shift
    echo "################ $titulo ################"
    for f in "$@"; do
        [ -f "$f" ] || continue
        echo
        echo "=== ./cparser $f ==="
        ./cparser "$f"
        echo "--- rc=$? ---"
    done
    echo
}

run_dir "TESTS SUELTOS" tests/ok_*.c tests/bad_*.c
run_dir "VALIDOS"                      tests/validos/*.c
run_dir "CON ERRORES"                  tests/con_errores/*.c