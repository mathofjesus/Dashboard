#!/bin/sh
# contrato-diff.sh — sobe o servidor C e o Java sobre a MESMA amostra e compara
# todas as rotas. Foi o portão da fase F5; o C foi removido na F10, então hoje
# ele só roda com um binário C de referência apontado em C_BIN (por exemplo,
# compilado a partir de um checkout anterior). Sem C_BIN, recusa em vez de
# comparar o Java com ele mesmo.
set -eu

AQUI=$(cd "$(dirname "$0")" && pwd)
RAIZ=$(cd "$AQUI/../.." && pwd)
JAR="$AQUI/../target/dashboard.jar"

[ -n "${C_BIN:-}" ] && [ -x "${C_BIN:-}" ] || {
    echo "contrato-diff: defina C_BIN com o binário C de referência (o C foi removido na F10)" >&2
    exit 2
}
[ "$(cd "$(dirname "$C_BIN")" && pwd)/$(basename "$C_BIN")" != "$RAIZ/dashboard" ] || {
    echo "contrato-diff: C_BIN aponta para o wrapper Java; informe o binário C de verdade" >&2
    exit 2
}
[ -f "$JAR" ] || { echo "contrato-diff: $JAR não existe (rode make)" >&2; exit 2; }

TMPC=$(mktemp -d "${TMPDIR:-/tmp}/contrato-c.XXXXXX")
TMPJ=$(mktemp -d "${TMPDIR:-/tmp}/contrato-j.XXXXXX")
PIDC=""
PIDJ=""

limpa() {
    [ -n "$PIDC" ] && kill "$PIDC" 2>/dev/null || true
    [ -n "$PIDJ" ] && kill "$PIDJ" 2>/dev/null || true
    rm -rf "$TMPC" "$TMPJ"
}
trap limpa EXIT INT TERM

# Amostra determinística nos dois lados; o índice tem de ser byte a byte igual.
(cd "$TMPC" && "$C_BIN" --amostra) >/dev/null
(cd "$TMPJ" && java -jar "$JAR" --amostra) >/dev/null
cmp "$TMPC/data/processed/modelo.idx" "$TMPJ/data/processed/modelo.idx" \
    || { echo "contrato-diff: modelo.idx do C e do Java diferem" >&2; exit 1; }

# Se as malhas reais existem, leva-as para os dois diretórios para comparar
# também a geografia (sem isso as rotas respondem 503 nos dois e não provam nada).
for f in geo_estados.json geo_municipios.json; do
    if [ -f "$RAIZ/data/processed/$f" ]; then
        ln -s "$RAIZ/data/processed/$f" "$TMPC/data/processed/$f"
        ln -s "$RAIZ/data/processed/$f" "$TMPJ/data/processed/$f"
    fi
done

PORTA=${PORTA:-18100}
while [ "$PORTA" -lt 18200 ]; do
    if [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 1 "http://127.0.0.1:$PORTA/" 2>/dev/null || true)" = "000" ]; then
        break
    fi
    PORTA=$((PORTA + 1))
done
PC=$PORTA
PJ=$((PORTA + 1))

(cd "$TMPC" && exec "$C_BIN" --porta "$PC" --web "$RAIZ/web") >"$TMPC/servidor.log" 2>&1 &
PIDC=$!
(cd "$TMPJ" && exec java -jar "$JAR" --porta "$PJ" --web "$RAIZ/web") >"$TMPJ/servidor.log" 2>&1 &
PIDJ=$!

i=0
while [ "$i" -lt 150 ]; do
    c=$(curl -s -o /dev/null -w '%{http_code}' --max-time 1 "http://127.0.0.1:$PC/api/opcoes" 2>/dev/null || true)
    j=$(curl -s -o /dev/null -w '%{http_code}' --max-time 1 "http://127.0.0.1:$PJ/api/opcoes" 2>/dev/null || true)
    [ "$c" = "200" ] && [ "$j" = "200" ] && break
    i=$((i + 1))
    sleep 0.2
done
[ "$c" = "200" ] && [ "$j" = "200" ] || {
    echo "contrato-diff: servidor não subiu (C=$c Java=$j)" >&2
    echo "--- C ---" >&2; tail -20 "$TMPC/servidor.log" >&2
    echo "--- Java ---" >&2; tail -20 "$TMPJ/servidor.log" >&2
    exit 1
}

python3 "$AQUI/contrato_diff.py" "http://127.0.0.1:$PC" "http://127.0.0.1:$PJ"
