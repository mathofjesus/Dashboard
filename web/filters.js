/* web/filters.js — painel de filtros.
 *
 * Separado em duas metades de propósito:
 *
 *   - As funções puras (asValidas, textoOpcoes, paramsDaConsulta) não tocam em
 *     DOM. É onde mora a decisão que a spec torna obrigatória: **"" e ".."
 *     nunca viram 0**. Um <select> sem seleção entrega "", e um parse
 *     ingênuo transforma isso em 0 — que o servidor rejeita com um 400
 *     confuso em vez de dizer "escolha um ano".
 *
 *   - As funções de DOM recebem `doc` por parâmetro, então este módulo
 *     também é importável em node e testável sem jsdom.
 *
 * "Respeitar o que o servidor devolveu" é literal: as listas de UF e de ano
 * são exatamente o que veio de /api/opcoes. Nada é inventado, nenhum ano é
 * completado por identidade entre o que o seletor mostra e o que a série tem.
 */

import { esc } from './api.js';

/** Opções de fonte, na ordem em que aparecem no painel. */
export const OPCOES_FONTE = [
    { valor: 'ibge', rotulo: 'IBGE' },
    { valor: 'saude', rotulo: 'Saúde' },
];

/**
 * Indicadores por fonte.
 *
 * A fonte decide o que é oferecido: população não é dado de saúde, e leitos
 * não é dado do IBGE. Trocar a fonte tem que trocar a lista, senão o usuário
 * escolhe um indicador que a fonte não tem e recebe erro do servidor.
 */
export const OPCOES_INDICADOR = [
    { valor: 'populacao', rotulo: 'População', fontes: ['ibge'] },
    { valor: 'estabelecimentos', rotulo: 'Estabelecimentos', fontes: ['ibge'] },
    { valor: 'leitos', rotulo: 'Leitos', fontes: ['saude'] },
    { valor: 'uti', rotulo: 'UTI', fontes: ['saude'] },
    { valor: 'dengue', rotulo: 'Dengue', fontes: ['saude'] },
];

/** Nível de agregação. Só existem dois; a API não aceita outros. */
export const OPCOES_NIVEL = [
    { valor: 'municipio', rotulo: 'Município' },
    { valor: 'uf', rotulo: 'UF' },
];

/** Ordenações. `codigo` é o que vai para a query. */
export const ORDENS = [
    { codigo: 'valor_desc', rotulo: 'Maior primeiro' },
    { codigo: 'valor_asc', rotulo: 'Menor primeiro' },
    { codigo: 'nome_asc', rotulo: 'Nome (A–Z)' },
];

/** Rótulos exibidos na ordenação, na ordem do array acima. */
export const OPCOES_ORDENAR = ORDENS.map((o) => ({ valor: o.codigo, rotulo: o.rotulo }));

/** Limites oferecidos, em número de linhas da tabela. */
export const OPCOES_LIMITE = [50, 100, 250, 500];

/** Rótulo da opção "todas as UFs" — valor vazio, que o servidor trata como ausente. */
export const ROTULO_TODAS_UFS = 'Todas as UFs';

/* ------------------------------------------------------------------ */
/* Funções puras                                                        */
/* ------------------------------------------------------------------ */

/**
 * Lê um ano do formulário.
 *
 * Devolve `null` — não 0 — para "", "   ", "..", "0", "abcd" ou "20a4". O
 * chamador omitirá o parâmetro da query nesse caso, que é o comportamento
 * certo: melhor não filtrar por ano do que filtrar por um ano que não existe.
 */
export function asValidas(valor) {
    if (valor === null || valor === undefined) return null;
    const bruto = String(valor).trim();
    if (bruto === '') return null;
    /* strtol sem sinal: "20a4" e "2024-01" têm que ser rejeitados, não
     * truncados para 20 e 2024. */
    if (!/^\d+$/.test(bruto)) return null;
    const n = parseInt(bruto, 10);
    if (!Number.isInteger(n) || n <= 0) return null;
    return n;
}

/**
 * Lê um inteiro com piso e teto.
 *
 * Usado no limite: um valor fora da faixa vira `null` (parâmetro omitido) em
 * vez de 0, que o servidor leria como "sem limite" — resultado oposto ao que
 * o usuário pediu.
 */
export function inteiroValido(valor, min, max) {
    const bruto = String(valor === null || valor === undefined ? '' : valor).trim();
    if (bruto === '') return null;
    if (!/^-?\d+$/.test(bruto)) return null;
    const n = parseInt(bruto, 10);
    if (!Number.isInteger(n)) return null;
    if (n < min) return null;
    if (n > max) return null;
    return n;
}

/** Indicadores válidos para uma fonte. */
export function indicadoresDaFonte(fonte) {
    return OPCOES_INDICADOR.filter((o) => o.fontes.includes(fonte));
}

/** Primeiro indicador da fonte — o padrão quando nada foi escolhido ainda. */
export function indicadorPadrao(fonte) {
    const lista = indicadoresDaFonte(fonte);
    return lista.length ? lista[0].valor : null;
}

/**
 * Monta os parâmetros de /api/consulta a partir do estado do formulário.
 *
 * Devolve um objeto, não uma string: a montagem da query (escapar, omitir
 * ausente) é de `api.js`. Ter as duas coisas chamadas `montarQuery` com
 * formatos diferentes foi a origem de um bug real aqui — quem importava pelo
 * nome errado recebia um objeto no lugar de uma query.
 *
 * Só entra o que foi preenchido. `ano` e `limite` passam pelos validadores
 * acima, então nunca entram como 0 por acidente.
 */
export function paramsDaConsulta(estado) {
    const e = estado || {};
    const params = {};
    if (e.fonte) params.fonte = e.fonte;
    if (e.uf) params.uf = e.uf;
    if (e.nivel) params.nivel = e.nivel;

    const ano = asValidas(e.ano);
    if (ano !== null) params.ano = ano;

    if (e.indicador) params.indicador = e.indicador;
    if (e.busca && String(e.busca).trim() !== '') params.busca = String(e.busca).trim();
    if (e.ordenar) params.ordenar = e.ordenar;

    const limite = inteiroValido(e.limite, 1, 5570);
    if (limite !== null) params.limite = limite;

    return params;
}

/**
 * Formata um valor para exibição.
 *
 * `-1` é a sentinela de "não existe" no aggregate. Mostrar isso como 0
 * mentiria duas vezes: diria que o município tem zero quando o que existe é
 * "nenhum dado publicado". Por isso o ausente é "--".
 */
export function textoOpcoes(valor) {
    if (valor === null || valor === undefined) return '--';
    const n = Number(valor);
    if (!Number.isFinite(n)) return '--';
    if (n < 0) return '--';
    return n.toLocaleString('pt-BR');
}

/**
 * O substantivo na forma que o número pede.
 *
 * `plural(1, 'município', 'municípios')` devolve "município"; `plural(2, …)` devolve
 * "municípios". Em português o plural já é o mais comum, e é por isso que o
 * padrão "escreve o plural e reza" produz "1 municípios" — que foi o que
 * aparecia no cartão do anel ("1 estados no resultado") e no cartão do mapa
 * ("1 localidades no resultado").
 *
 * O que decide é 1 contra o resto, e não o valor absoluto: 0 é plural em
 * português ("0 municípios"), 1 é singular, e -1 aqui nunca chega porque quem
 * chama já tratou o ausente como "--" antes.
 */
export function plural(n, singular, plural_) {
    const num = Number(n);
    const um = Number.isFinite(num) && Math.abs(num) === 1;
    return um ? singular : plural_;
}

/* ------------------------------------------------------------------ */
/* DOM (doc injetado)                                                  */
/* ------------------------------------------------------------------ */

function opcao(doc, valor, rotulo, selecionado) {
    const el = doc.createElement('option');
    el.value = valor;
    el.textContent = rotulo;
    if (selecionado) el.setAttribute('selected', 'selected');
    return el;
}

/** Esvazia um <select> deixando apenas as opções fornecidas. */
export function preencher(doc, select, itens, valorAtual) {
    if (!select) return select;
    while (select.firstChild) select.removeChild(select.firstChild);
    for (const it of itens) {
        select.appendChild(opcao(doc, it.valor, it.rotulo, String(it.valor) === String(valorAtual)));
    }
    if (valorAtual !== undefined && valorAtual !== null) select.value = String(valorAtual);
    return select;
}

/** Preenche a fonte e os indicadores que ela oferece. */
export function preencherFontes(doc, fonteAtual) {
    const f = doc.getElementById('fonte');
    preencher(doc, f, OPCOES_FONTE, fonteAtual);
    preencherIndicadores(doc, fonteAtual);
    return f;
}

/** Só os indicadores da fonte escolhida — o resto não é oferecível. */
export function preencherIndicadores(doc, fonte, selecionado) {
    const lista = indicadoresDaFonte(fonte);
    const alvo = selecionado && lista.some((o) => o.valor === selecionado)
        ? selecionado
        : (lista.length ? lista[0].valor : '');
    preencher(doc, doc.getElementById('indicador'),
        lista.map((o) => ({ valor: o.valor, rotulo: o.rotulo })), alvo);
    return alvo;
}

export function preencherNiveis(doc, selecionado) {
    preencher(doc, doc.getElementById('nivel'),
        OPCOES_NIVEL, selecionado || OPCOES_NIVEL[0].valor);
}

export function preencherOrdenacoes(doc, selecionado) {
    preencher(doc, doc.getElementById('ordenar'),
        OPCOES_ORDENAR, selecionado || ORDENS[0].codigo);
}

export function preencherLimites(doc, selecionado) {
    const itens = OPCOES_LIMITE.map((n) => ({ valor: String(n), rotulo: `${n} linhas` }));
    const alvo = selecionado !== undefined && selecionado !== null && String(selecionado) !== ''
        ? String(selecionado)
        : String(OPCOES_LIMITE[1]);
    preencher(doc, doc.getElementById('limite'), itens, alvo);
    return alvo;
}

/**
 * Preenche UF e ano com o que o servidor devolveu.
 *
 * Recebe o resultado já normalizado de `opcoes()`. A lista de UFs começa
 * com "Todas as UFs" (valor vazio) porque nem sempre se quer filtrar por
 * estado. A lista de anos é a do servidor, sem complemento: se 2022 não
 * veio, 2022 não aparece.
 */
export function preencherOpcoes(doc, resultado, anoAtual, ufAtual) {
    const ufs = resultado && Array.isArray(resultado.ufs) ? resultado.ufs : [];
    const anos = resultado && Array.isArray(resultado.anos) ? resultado.anos : [];

    const selUf = doc.getElementById('uf');
    const itensUf = [{ valor: '', rotulo: ROTULO_TODAS_UFS }]
        .concat(ufs.map((u) => ({ valor: u, rotulo: u })));
    const alvoUf = ufAtual && ufs.includes(ufAtual) ? ufAtual : '';
    preencher(doc, selUf, itensUf, alvoUf);

    const selAno = doc.getElementById('ano');
    const itensAno = anos.map((a) => ({ valor: String(a), rotulo: String(a) }));
    /* Mantém o ano escolhido se o servidor ainda o oferece; senão usa o mais
     * recente, que é o que o usuário quer ver primeiro. */
    const alvoAno = (anoAtual && anos.some((a) => String(a) === String(anoAtual)))
        ? String(anoAtual)
        : /* I7: o maior ano, e não o primeiro da lista. `/api/opcoes` devolve os anos
  * em ordem crescente, então `anos[0]` era 2001 — o mais velho de uma série
  * de 22 — e o painel abria com "Dado publicado em 2001". */
 (anos.length ? String(anos[anos.length - 1]) : '');

    if (!itensAno.length) {
        while (selAno.firstChild) selAno.removeChild(selAno.firstChild);
        /* Um <option> vazio com texto explica o porquê sem parecer quebrado. */
        selAno.appendChild(opcao(doc, '', 'Nenhum ano disponível', true));
        selAno.value = '';
    } else {
        preencher(doc, selAno, itensAno, alvoAno);
    }

    return { ano: alvoAno, uf: alvoUf };
}

/** Lê o estado do formulário a partir do DOM. */
export function lerFormulario(doc) {
    const v = (id) => {
        const el = doc.getElementById(id);
        return el ? el.value : '';
    };
    return {
        fonte: v('fonte'),
        uf: v('uf'),
        ano: v('ano'),
        indicador: v('indicador'),
        nivel: v('nivel'),
        busca: v('busca'),
        ordenar: v('ordenar'),
        limite: v('limite'),
    };
}
