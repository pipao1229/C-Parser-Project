fail=0
for f in tests/ok_*.c tests/sha256.c tests/cJSON.c; do
    [ -f "$f" ] || continue
    out=$(./cparser "$f" 2>&1)
    [ -z "$out" ] || { echo "FALLA (esperaba 0 errores): $f"; echo "$out" | head -5; fail=1; }
done

# mismo archivo con CRLF debe comportarse igual
tr -d '\r' < tests/ok_splice.c | awk '{ printf "%s\r\n", $0 }' > /tmp/ok_splice_crlf.c
out=$(./cparser /tmp/ok_splice_crlf.c 2>&1)
[ -z "$out" ] || { echo "FALLA CRLF"; echo "$out" | head -5; fail=1; }

# los bad_* deben dar al menos un error y nunca segfault (rc=1, no 139)
for f in tests/bad_*.c tests/md5.c tests/map.c tests/re.c tests/aes.c; do
    [ -f "$f" ] || continue
    ./cparser "$f" >/dev/null 2>&1
    rc=$?
    [ $rc -eq 1 ] || { echo "FALLA (esperaba rc=1, dio $rc): $f"; fail=1; }
done
[ $fail -eq 0 ] && echo "todo OK"