#!/bin/sh
# tests/test_cnes_sample.sh — monta a fixture do CNES para o teste.
#
# Por que uma fixture e não o dado real: o CSV do CNES tem 231.007.483 bytes e
# 637.996 linhas, e nenhum `make test` pode depender disso. A fixture tem 50
# linhas e é gerada aqui, com as MESMAS 36 colunas do arquivo real e na mesma
# ordem — um cabeçalho inventado passaria num parser que lê as colunas pela
# posição errada, que é exatamente o defeito que a poda precisa evitar.
#
# Os 36 nomes vêm do arquivo real (medido, não de memória): CO_IBGE é a 4ª,
# NO_RAZAO_SOCIAL a 6ª, NO_FANTASIA a 7ª, DS_NIVEL_HIERARQUIA a 12ª,
# NU_LATITUDE a 22ª e NU_LONGITUDE a 23ª.
#
# As duas coisas que a fixture reproduz de propósito:
#
#   - `CO_IBGE` SEM aspas, enquanto o arquivo real põe tudo entre aspas. O
#     parser tem de aceitar os dois, e um fixture só com aspas não exercita
#     esse caminho.
#   - `NO_RAZAO_SOCIAL` com acento em ISO-8859-1 (0xCD = Í, 0xE3 = ã). O CSV
#     real é latin-1 — medido: os 62 bytes não-ASCII dos 231 MB estão em
#     telefone, e-mail e site, e são 0xE3/0xCD/0xAA de latin-1, não UTF-8. Os
#     acentos caem por acaso fora das seis colunas podadas, mas a
#     transcodificação é aplicada na mesma passagem e não se pode confiar em
#     "hoje não tem acento aqui" para pular a conversão.
#
# Uso: tests/test_cnes_sample.sh <diretorio-de-saida>
# Escreve <saida>/cnes_amostra.csv.
set -eu

DESTINO=${1:?uso: test_cnes_sample.sh <diretorio>}
mkdir -p "$DESTINO"
CSV="$DESTINO/cnes_amostra.csv"
ZIP="$DESTINO/cnes_amostra.zip"

# As 36 colunas do arquivo real, na ordem real.
CABECALHO='CO_CNES;CO_UNIDADE;CO_UF;CO_IBGE;NU_CNPJ_MANTENEDORA;NO_RAZAO_SOCIAL;NO_FANTASIA;CO_NATUREZA_ORGANIZACAO;DS_NATUREZA_ORGANIZACAO;TP_GESTAO;CO_NIVEL_HIERARQUIA;DS_NIVEL_HIERARQUIA;CO_ESFERA_ADMINISTRATIVA;DS_ESFERA_ADMINISTRATIVA;CO_ATIVIDADE;TP_UNIDADE;CO_CEP;NO_LOGRADOURO;NU_ENDERECO;NO_BAIRRO;NU_TELEFONE;NU_LATITUDE;NU_LONGITUDE;CO_TURNO_ATENDIMENTO;DS_TURNO_ATENDIMENTO;NU_CNPJ;NO_EMAIL;CO_NATUREZA_JUR;ST_CENTRO_CIRURGICO;ST_CENTRO_OBSTETRICO;ST_CENTRO_NEONATAL;ST_ATEND_HOSPITALAR;ST_SERVICO_APOIO;ST_ATEND_AMBULATORIAL;CO_MOTIVO_DESAB;CO_AMBULATORIAL_SUS'

printf '%s\n' "$CABECALHO" > "$CSV"

# 50 linhas de dados. Três com nome acentuado em latin-1, uma sem município
# (CO_IBGE vazio) e uma com "..": os dois marcadores de ausência que a spec §4
# exige que virem -1 e nunca 0.
i=1
while [ "$i" -le 50 ]; do
    ibge=$(printf '%07d' $((1100000 + i)))
    razao="CENTRO DE SAUDE $i"
    fantasia="UBS $*"
    nivel="02 - CENTRO DE SAUDE"
    lat="-8.0$((i % 100))"
    lon="-63.0$((i % 100))"

    case "$i" in
        1) razao=$(printf 'POLICLINICA DR JAMACI DE MEDEIROS \315') ;;
        2) razao=$(printf 'HOSPITAL DE BASE S\303O LU\314S') ;;
        3) razao=$(printf 'UBS JARDIM \303STIMA') ;;
        12) ibge='..' ;;
        13) ibge='' ;;
    esac

    # CO_IBGE sem aspas (de propósito); o resto com aspas, como o real.
    printf '2%s;2%s;11;%s;00000000000151;"%s";"%s";1234;"SAUDE";"PUBLICO";"S";"2";"%s";"1";"MUNICIPAL";"2";"01";"76800-000";"RUA %s";"%s";"CENTRO";"11 99999-0000";"%s";"%s";"12";"24H";"00000000000191";"saude%s@exemplo.br";"PUBLICIDADE";"NAO";"NAO";"NAO";"SIM";"SIM";"NAO";"0";"0"\n' \
        "$(printf '%03d' "$i")" "$(printf '%03d' "$i")" "$ibge" "$razao" "$fantasia" \
        "$nivel" "$i" "$i" "$lat" "$lon" "$i" >> "$CSV"

    i=$((i + 1))
done

# O ZIP NÃO é montado aqui: este sistema não tem o binário `zip`, e depender
# dele (ou de python3) para montar uma fixture colocaria uma ferramenta externa
# no caminho de `make test`. O ZIP é escrito pelo próprio teste, em C, com o
# método stored e o crc32 da zlib — que já é linkada em TEST_BIN. Ver
# `escreve_zip_stored` em tests/test_saude.c.
#
# O que este script serve é o CSV legível, para alguém conferir o formato sem
# rodar nada. Os bytes são latin-1 de propósito: abrindo no navegador pode
# parecer quebrado, e está certo.

printf '%s\n' "$CSV"