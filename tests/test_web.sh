#!/bin/sh
# tests/test_web.sh — verifica os estáticos do front servidos pelo servidor de
# verdade, não os arquivos em disco.
#
# Motivo: existe uma classe de bug que nenhum teste de unitário pega — o
# servidor responder 404 para app.js, ou servir .js com o MIME errado, e a
# página abrir em branco sem nenhuma pista. Estes testes conversam com o
# HTTP.
#
# O script sobe o próprio servidor numa porta livre, espera ficar pronto,
# consulta e derruba. Se já houver um servidor na porta, ele o reaproveita.
#
# Uso: tests/test_web.sh [porta]
set -eu

RAIZ=$(cd "$(dirname "$0")/.." && pwd)
PORTA=${1:-8099}
BIN="$RAIZ/dashboard"
BASE="http://127.0.0.1:$PORTA"

falhas=0
testes=0

# `falha` imprime o detalhe e conta a verificação; `confere` conta e imprime
# só o rótulo. As duas contam exatamente uma vez — antes elas somavam duas,
# e o relatório dizia "1 de 15" num total que era 16, o que sugeria que
# hadn't sido verificado.
falha() {
    # $1 = o que era esperado, $2 = o que aconteceu, $3 = o que foi testado
    testes=$((testes + 1))
    falhas=$((falhas + 1))
    printf 'FALHOU: %s\n  esperado: %s\n  obtido:   %s\n' "$3" "$1" "$2"
}

confere() {
    # $1 = rótulo, $2 = condição (0 = passou)
    testes=$((testes + 1))
    if [ "$2" -ne 0 ]; then
        falhas=$((falhas + 1))
        printf 'FALHOU: %s\n' "$1"
    fi
}

# Só considera falha se o servidor respondeu; "não respondeu" é outro teste.
status() {
    curl -s -o /dev/null -w '%{http_code}' --max-time 5 "$1" 2>/dev/null || printf '000'
}

corpo() {
    curl -sf --max-time 5 "$1" 2>/dev/null || true
}

if [ ! -x "$BIN" ]; then
    printf 'test_web: %s não existe — rode make antes\n' "$BIN" >&2
    exit 1
fi

# --- sobe o servidor ---------------------------------------------------
SUBIU=0
PID_SRV=""
LOG_SRV="${TMPDIR:-/tmp}/test_web_servidor.$$.log"
if [ "$(status "$BASE/index.html")" = "200" ]; then
    printf -- '--- web: reaproveitando o servidor em %s\n' "$BASE"
else
    printf -- '--- web: subindo o servidor em %s\n' "$BASE"
    ( cd "$RAIZ" && exec "$BIN" --porta "$PORTA" ) >"$LOG_SRV" 2>&1 &
    PID_SRV=$!
    # espera o servidor responder
    i=0
    while [ "$i" -lt 50 ]; do
        if [ "$(status "$BASE/index.html")" != "000" ]; then
            SUBIU=1
            break
        fi
        i=$((i + 1))
        sleep 0.1
    done
    if [ "$SUBIU" -eq 0 ]; then
        printf 'FALHOU: servidor não respondeu na porta %s\n' "$PORTA"
        printf -- '--- log do servidor ---\n'
        cat "$LOG_SRV" 2>/dev/null || true
        kill "$PID_SRV" 2>/dev/null || true
        rm -f "$LOG_SRV"
        exit 1
    fi
fi

# Derruba o servidor no fim, por qualquer caminho de saída. Guarda o PID
# explicitamente em vez de usar %1: em shell não interativo o "%1" não é
# fiável, e o `kill` errado mata o próprio script (o que aconteceu).
para_servidor() {
    if [ "$SUBIU" -eq 1 ] && [ -n "$PID_SRV" ]; then
        kill "$PID_SRV" 2>/dev/null || true
        wait "$PID_SRV" 2>/dev/null || true
    fi
    [ -n "$LOG_SRV" ] && rm -f "$LOG_SRV"
    return 0
}
trap para_servidor EXIT INT TERM

# --- o servidor entrega cada arquivo que o front precisa --------------
# Um 200 e um corpo com o que se espera são a mesma verificação; quem imprime
# o detalhe é `falha`, que já conta. Por isso não há `confere` em paralelo.
S_IDX=$(status "$BASE/index.html")
[ "$S_IDX" = 200 ] || falha 'HTTP 200' "$S_IDX" "index.html responde 200"

for arq in app.js api.js filters.js charts.js styles.css; do
    S=$(status "$BASE/$arq")
    [ "$S" = 200 ] || falha 'HTTP 200' "$S" "$arq responde 200"
done

# --- o brief pede exatamente estes dois -------------------------------
# Passo 6 do brief:
if corpo "$BASE/index.html" | grep -q 'id="filtrar"'; then
    confere "index.html contém id=\"filtrar\"" 0
else
    falha 'id="filtrar"' 'ausente' "index.html"
fi

if corpo "$BASE/app.js" | grep -q 'consultado'; then
    confere "app.js menciona consultado" 0
else
    falha 'a palavra consultado' 'ausente' "app.js"
fi

# --- .js precisa de MIME de módulo, senão o browser recusa -----------
CT=$(curl -s -o /dev/null -w '%{content_type}' --max-time 5 "$BASE/app.js" 2>/dev/null || printf '')
case "$CT" in
    *javascript*) confere "app.js servido como JavaScript ($CT)" 0 ;;
    *) falha 'Content-Type de JavaScript' "$CT" "MIME de app.js" ;;
esac

# --- estado vazio: os textos que a spec exige -----------------------
IDX=$(corpo "$BASE/index.html")
for texto in 'Ajuste os filtros e clique em Filtrar' 'Sem dados — clique em Filtrar'; do
    if printf '%s' "$IDX" | grep -qF "$texto"; then
        confere "estado vazio: \"$texto\"" 0
    else
        falha "\"$texto\"" 'ausente' "index.html"
    fi
done

# Os KPIs têm que começar em "--", não em 0 nem vazio.
if printf '%s' "$IDX" | grep -qE '\-\-'; then
    confere "estado vazio: KPIs com --" 0
else
    falha 'placeholder --' 'ausente' "KPIs em index.html"
fi

# --- o app não busca nada sozinho ------------------------------------
# Um fetch no topo do módulo é o que enche a tela sem o usuário pedir.
if corpo "$BASE/app.js" | grep -nE '^\s*(await\s+)?(consultar|opcoes)\(' ; then
    falha 'nenhuma consulta no topo do módulo' 'chamada no escopo de módulo' "app.js"
else
    confere "app.js não chama consultar()/opcoes() no escopo de módulo" 0
fi

# --- os textos do front têm acento ----------------------------------
# Um " proximas" ou "publico" denuncia fonte escrita sem acento, que foi o
# defeito I4 na Tarefa 1.
if corpo "$BASE/index.html" | grep -qE '>[^<]*(proximas|publicos|ano invalido|municipio)[^<]*<' \
   && ! corpo "$BASE/index.html" | grep -q 'Próximas\|próximas\|públicos\|Município'; then
    falha 'texto sem acento' 'encontrado' "index.html"
else
    confere "textos do front com acento" 0
fi

# --- a API responde ----------------------------------------------
S_OPC=$(status "$BASE/api/opcoes?fonte=ibge&indicador=populacao")
confere "api/opcoes responde 200 (obtido $S_OPC)" \
    "$([ "$S_OPC" = 200 ] && echo 0 || echo 1)"

# 2022 é a lacuna conhecida da série; não pode ser oferecido.
if corpo "$BASE/api/opcoes?fonte=ibge&indicador=populacao" | grep -q '2022'; then
    falha 'api/opcoes sem 2022' '2022 presente' "api/opcoes"
else
    confere "api/opcoes não oferece 2022" 0
fi

# --- cleanup ------------------------------------------------------
# O trap EXIT já derruba o servidor; aqui é só o relatório.

# `printf '---…'` quebra em dash, que lê o texto inicial como opção; o
# formato vai como primeiro argumento.
printf '%s\n' "--- web: $([ "$falhas" -eq 0 ] && echo OK || echo FALHOU)"
if [ "$falhas" -gt 0 ]; then
    printf '%s de %s verificações do web falharam\n' "$falhas" "$testes"
    exit 1
fi
printf 'OK: %s verificações do web passaram\n' "$testes"
