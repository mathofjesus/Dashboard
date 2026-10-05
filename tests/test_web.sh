#!/bin/sh
# tests/test_web.sh — verifica os estáticos do front servidos pelo servidor de
# verdade, não os arquivos em disco.
#
# Motivo: existe uma classe de bug que nenhum teste de unitário pega — o
# servidor responder 404 para app.js, ou servir .js com o MIME errado, e a
# página abrir em branco sem nenhuma pista. Estes testes conversam com o HTTP.
#
# O teste é hermético, e isso mudou por causa de dois achados:
#
#   I14  A porta era fixa (8099) e o script *reaproveitava* um servidor que
#        estivesse lá. Quem deixasse um servidor no ar rodando com o código de
#        ontem fazia este teste passar contra o servidor errado — e, pior,
#        derrubava o servidor de outra pessoa no `trap`. Agora a porta é
#        escolhida livre e o servidor é sempre um filho deste script.
#
#   C2   O servidor passou a recusar subir sem dado processado (§10). Com a
#        árvore limpa o teste passava a falhar por falta de índice, e com um
#        índice de 3 bytes escrito à mão passava por acidente. Agora o teste
#        roda num diretório temporário, semeia `./dashboard --amostra` (que não
#        baixa nada) e sobe o servidor dali. Nada do diretório de trabalho é
#        lido, nada é escrito e nenhuma rede é usada.
#
# Uso: tests/test_web.sh [porta]
set -eu

RAIZ=$(cd "$(dirname "$0")/.." && pwd)
BIN="$RAIZ/dashboard"

falhas=0
testes=0

# `falha` imprime o detalhe e conta a verificação; `confere` conta e imprime
# só o rótulo. As duas contam exatamente uma vez — antes elas somavam duas,
# e o relatório dizia "1 de 15" num total que era 16, o que sugeria que
# não tinha sido verificado.
falha() {
    testes=$((testes + 1))
    falhas=$((falhas + 1))
    printf 'FALHOU: %s\n  esperado: %s\n  obtido:   %s\n' "$3" "$1" "$2"
}

confere() {
    testes=$((testes + 1))
    if [ "$2" -ne 0 ]; then
        falhas=$((falhas + 1))
        printf 'FALHOU: %s\n' "$1"
    fi
}

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

# --- diretório de trabalho isolado -----------------------------------
# `web` é um link para a árvore real: o que se testa é o arquivo que será
# servido, não uma cópia que pode divergir do original.
TMP=$(mktemp -d "${TMPDIR:-/tmp}/test_web.XXXXXX")
ln -s "$RAIZ/web" "$TMP/web"

PID_SRV=""
LOG_SRV="$TMP/servidor.log"

para_servidor() {
    if [ -n "$PID_SRV" ]; then
        kill "$PID_SRV" 2>/dev/null || true
        wait "$PID_SRV" 2>/dev/null || true
        PID_SRV=""
    fi
    return 0
}
trap 'para_servidor; rm -rf "$TMP"' EXIT INT TERM

# O índice que o portão de primeira execução exige. `--amostra` grava três
# municípios e três anos, determinísticos e sem rede — é o suficiente para
# /api/opcoes e /api/consulta responderem 200 com conteúdo de verdade.
if ! ( cd "$TMP" && "$BIN" --amostra ) >"$TMP/amostra.log" 2>&1; then
    printf 'FALHOU: --amostra não gravou o índice\n'
    cat "$TMP/amostra.log" 2>/dev/null || true
    exit 1
fi
[ -s "$TMP/data/processed/modelo.idx" ] || {
    printf 'FALHOU: --amostra não deixou data/processed/modelo.idx\n'
    exit 1
}
confere "a amostra deixa um índice que o portão aceita" 0

# --- porta livre ------------------------------------------------------
# Sonda a partir da porta pedida (ou de uma alta e improvável) até achar uma
# que não responda. Reaproveitar um servidor alheio é justamente o defeito
# I14, então aqui a única porta aceitável é a que ninguém está usando.
PORTA=${1:-0}
if [ "$PORTA" -eq 0 ]; then
    PORTA=$(( 18000 + ($$ % 2000) ))
fi
i=0
while [ "$i" -lt 20 ]; do
    if [ "$(status "http://127.0.0.1:$PORTA/index.html")" = "000" ]; then
        break
    fi
    PORTA=$((PORTA + 1))
    i=$((i + 1))
done
BASE="http://127.0.0.1:$PORTA"

printf -- '--- web: subindo o servidor em %s (cwd temporário)\n' "$BASE"
( cd "$TMP" && exec "$BIN" --porta "$PORTA" --web web ) >"$LOG_SRV" 2>&1 &
PID_SRV=$!

SUBIU=0
i=0
while [ "$i" -lt 100 ]; do
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
    exit 1
fi

# --- o servidor entrega cada arquivo que o front precisa --------------
S_IDX=$(status "$BASE/index.html")
[ "$S_IDX" = 200 ] || falha 'HTTP 200' "$S_IDX" "index.html responde 200"

for arq in app.js api.js filters.js charts.js styles.css; do
    S=$(status "$BASE/$arq")
    [ "$S" = 200 ] || falha 'HTTP 200' "$S" "$arq responde 200"
done

# --- o brief pede exatamente estes dois -------------------------------
if corpo "$BASE/index.html" | grep -q 'id="filtrar"'; then
    confere 'index.html contém id="filtrar"' 0
else
    falha 'id="filtrar"' 'ausente' "index.html"
fi

if corpo "$BASE/app.js" | grep -q 'consultado'; then
    confere 'app.js menciona consultado' 0
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
if printf '%s' "$IDX" | grep -qF -- '--'; then
    confere 'estado vazio: KPIs com --' 0
else
    falha 'placeholder --' 'ausente' "KPIs em index.html"
fi

# --- o app não busca nada sozinho ------------------------------------
# Um fetch no topo do módulo é o que enche a tela sem o usuário pedir.
if corpo "$BASE/app.js" | grep -nE '^\s*(await\s+)?(consultar|opcoes)\(' ; then
    falha 'nenhuma consulta no topo do módulo' 'chamada no escopo de módulo' "app.js"
else
    confere 'app.js não chama consultar()/opcoes() no escopo de módulo' 0
fi

# --- o app não desenha nada sozinho (spec §10, tarefa 10) -------------
# A mesma violação pelo outro lado: mesmo sem fetch, uma chamada de desenho no
# escopo de módulo pinta a tela na abertura, e "tudo em branco" deixa de valer.
# A guarda olha charts.js — que não pode desenhar fora de função — e app.js.
for arq in charts.js app.js; do
    # Âncora sem `\s*`: o que só acontece na coluna zero é chamada de escopo
    # de módulo. Uma chamada indentada está dentro de alguma função — e a
    # função só roda quando alguém pediu.
    ACHADO=$(corpo "$BASE/$arq" | grep -nE '^(await[[:space:]]+)?(linha|barras|donut|kpi|desenhar[A-Za-z]*)\(' || true)
    if [ -n "$ACHADO" ]; then
        falha 'nenhum desenho no escopo de módulo' "chamada no topo: $ACHADO" "$arq"
    else
        confere "$arq não desenha nada no escopo de módulo" 0
    fi
done

# O desenho é SVG do navegador, não biblioteca empacotada por baixo.
if corpo "$BASE/charts.js" | grep -q 'createElementNS'; then
    confere 'charts.js gera SVG com createElementNS' 0
else
    falha 'createElementNS' 'ausente' "charts.js"
fi

# A lacuna é a razão de existir desta tarefa: `segmentosLinha` precisa existir e
# cortar a série onde falta ano.
if corpo "$BASE/charts.js" | grep -q 'segmentosLinha'; then
    confere 'charts.js quebra a série em segmentos' 0
else
    falha 'segmentosLinha' 'ausente' "charts.js"
fi

# O cálculo tem de ser testável em node puro: um `document.` no escopo de módulo
# quebraria o `import` em node e derrubaria a suíte inteira do front.
if corpo "$BASE/charts.js" | grep -nE '^\s*document\.' ; then
    falha 'nenhum document. no escopo de módulo' 'uso do global no topo' "charts.js"
else
    confere 'charts.js não usa o global document no escopo de módulo' 0
fi

# --- os textos do front têm acento -----------------------------------
# I15: a guarda anterior era `grep sem acento && ! grep com acento`, e ela não
# podia falhar — bastava o index.html conter "Município" em qualquer canto para
# a segunda metade do `&&` desligar o teste inteiro, mesmo com texto sem
# acento ao lado. Aqui cada palavra é verificada sozinha e a verificação não
# tem escape: ou não há palavra sem acento em nenhum arquivo, ou o teste falha
# dizendo quais.
#
# A lista NÃO pode conter palavra que seja valor de protocolo. `municipio`,
# `serie`, `media`, `codigo` e `opcoes` aparecem sem acento porque são o
# token que vai na query string e a chave que vem no JSON — acentuá-los
# quebraria a chamada. O que está aqui é palavra que, escrita sem acento em
# texto, é erro de português.
PALAVRAS='proximas proximo publicos publico invalido invalida ultimo ultima
informacao numero possiveis possivel analise tambem configuracao descricao
selecao situacao estatistica historico relatorio atualizacao conexao
requisicao criterio periodo mediana referencia parametro parametros
padrao padroes versao opcoes-completo'
for arq in index.html app.js api.js filters.js charts.js; do
    ACHADO=''
    for p in $PALAVRAS; do
        case "$p" in *-*) continue ;; esac
        if corpo "$BASE/$arq" | grep -q "$p"; then
            ACHADO="$ACHADO $p"
        fi
    done
    if [ -n "$ACHADO" ]; then
        falha 'nenhuma palavra sem acento' "encontrada(s):$ACHADO" "texto de $arq"
    else
        confere "textos de $arq com acento" 0
    fi
done

# E o contrário, que é o que impede o teste de passar por lista vazia: o
# acento tem que estar lá. Um guarda que só sabe dizer "não achei o erro" é
# um guarda que passa em arquivo vazio.
for arq in index.html app.js; do
    ACHADO=''
    for p in 'município' 'Município' 'série' 'Média' 'públicos'; do
        if corpo "$BASE/$arq" | grep -qF "$p"; then
            ACHADO=1
        fi
    done
    if [ -z "$ACHADO" ]; then
        falha 'palavra acentuada' 'nenhuma encontrada' "acento presente em $arq"
    else
        confere "$arq traz texto acentuado" 0
    fi
done

# --- a API responde com o formato da spec §8 ------------------------
S_OPC=$(status "$BASE/api/opcoes?fonte=ibge&indicador=populacao")
confere "api/opcoes responde 200 (obtido $S_OPC)" \
    "$([ "$S_OPC" = 200 ] && echo 0 || echo 1)"

# As opções precisam trazer UFs de verdade — era o literal `"ufs": []` que
# deixava o filtro de UF do front sem nenhuma opção (achado C6). E o corpo
# tem que existir: uma verificação com `body == NULL` passa com corpo vazio.
OPC=$(corpo "$BASE/api/opcoes?fonte=ibge&indicador=populacao")
if [ -n "$OPC" ] && printf '%s' "$OPC" | grep -q '"ufs":\[' ; then
    confere 'api/opcoes traz a chave ufs' 0
else
    falha 'a chave ufs' "${OPC:-<corpo vazio>}" "api/opcoes"
fi
if printf '%s' "$OPC" | grep -q '"ufs":\[\]' ; then
    falha 'ufs com pelo menos uma UF' 'lista vazia' "api/opcoes"
else
    confere 'api/opcoes lista UFs do modelo' 0
fi
if printf '%s' "$OPC" | grep -q '"anos":\[' ; then
    confere 'api/opcoes traz a chave anos' 0
else
    falha 'a chave anos' 'ausente' "api/opcoes"
fi

# --- a consulta é real ------------------------------------------------
S_CON=$(status "$BASE/api/consulta?fonte=ibge&indicador=populacao&ano=2024")
confere "api/consulta responde 200 (obtido $S_CON)" \
    "$([ "$S_CON" = 200 ] && echo 0 || echo 1)"

CON=$(corpo "$BASE/api/consulta?fonte=ibge&indicador=populacao&ano=2024")
for chave in '"consulta":' '"total":' '"kpis":' '"registros":'; do
    if printf '%s' "$CON" | grep -qF "$chave"; then
        confere "api/consulta traz $chave" 0
    else
        falha "$chave" 'ausente' "api/consulta"
    fi
done
if printf '%s' "$CON" | grep -qF '"registros":[]' ; then
    falha 'registros com pelo menos uma linha' 'lista vazia' "api/consulta"
else
    confere 'api/consulta devolve registros' 0
fi
if printf '%s' "$CON" | grep -qF '"dados"' ; then
    falha 'sem a chave dados do formato antigo' '"dados" presente' "api/consulta"
else
    confere 'api/consulta usa o formato da spec, sem "dados"' 0
fi

# Ano que não existe é 400 com mensagem em português — não 200 com lista
# vazia, que é o que faria a tela mentir.
S_BAD=$(status "$BASE/api/consulta?fonte=ibge&indicador=populacao&ano=1899")
confere "ano inválido responde 400 (obtido $S_BAD)" \
    "$([ "$S_BAD" = 400 ] && echo 0 || echo 1)"

# --- relatório -------------------------------------------------------
printf '%s\n' "--- web: $([ "$falhas" -eq 0 ] && echo OK || echo FALHOU)"
if [ "$falhas" -gt 0 ]; then
    printf '%s de %s verificações do web falharam\n' "$falhas" "$testes"
    exit 1
fi
printf 'OK: %s verificações do web passaram\n' "$testes"