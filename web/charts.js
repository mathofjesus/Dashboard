/* web/charts.js — gráficos SVG do dashboard.
 *
 * Ruling C-5, aplicado já aqui: tudo que é *cálculo* neste arquivo é função
 * pura, sem `document`, sem `window`. É o que permite rodar os testes em node
 * (`node tests/test_web.mjs`) sem jsdom, que não é instalável aqui.
 *
 * A divisão é esta:
 *
 *   - `segmentosLinha`, `paresDaSerie`, `geometriaLinha`, `geometriaBarras`,
 *     `geometriaDonut` e companhia recebem dados e devolvem números. Nada
 *     elas desenham, nada elas medem. É aqui que mora o fato mais importante
 *     da visualização: a série histórica do IBGE tem lacuna em 2007, 2010,
 *     2022 e 2023, e essa lacuna precisa aparecer como um corte no gráfico.
 *     Ligar 2021 direto em 2024 afirmaria que a série é contínua, e ela não é —
 *     mostraria um crescimento inventado.
 *
 *   - `desenhar*` recebem um `doc` explícito e só convertem números em nós de
 *     SVG. `linha`, `barras`, `donut` e `kpi` são a porta do contrato do
 *     brief, com o documento vindo das opções. O DOM entra como argumento,
 *     nunca como pressuposto do módulo.
 *
 * A segunda regra que atravessa o arquivo inteiro: **ausente é lacuna, não é
 * zero**. Um ponto sem valor não vira ponto na linha, uma barra sem valor não
 * vira barra de altura zero (que na tela é indistinguível de "vale zero") e uma
 * fatia de tamanho zero não vira arco. As três direções erradas produzem a
 * mesma mentira: um gráfico que afirma um dado que ninguém mediu.
 */

import { textoOpcoes } from './filters.js';

/** Cor do que não tem dado. Cinza neutro, e nunca o extremo baixo da escala. */
export const COR_AUSENCIA = '#5a6470';

/** Cor da lacuna da série: é o vão que precisa de atenção, não o dado. */
export const COR_LACUNA = '#e3a008';

/**
 * Paleta categórica, na ordem em que as fatias aparecem.
 *
 * O verde é o acento do painel; as outras nove existem para que duas fatias
 * quaisquer sejam distinguíveis entre si, inclusive para quem não distingue
 * verde de vermelho. Nenhuma delas é a cor de ausência, que precisa ficar
 * fora do conjunto — senão "sem dado" pareceria uma fatia como as outras.
 */
const PALETA = [
    '#2fbf71', '#4aa3f0', '#e3a008', '#c07adf',
    '#f4564a', '#3fc7c7', '#8f9aa8', '#7fd45a',
    '#5f8de8', '#d86ba0',
];

/* ------------------------------------------------------------------ */
/* Predicados                                                          */
/* ------------------------------------------------------------------ */

/**
 * Um valor existe?
 *
 * `null`, `undefined`, `NaN` e os negativos são ausentes. O `-1` entra aqui
 * porque é a sentinela de "não existe" no aggregate do servidor; tratá-lo como
 * número colocaria um valor negativo no eixo.
 *
 * A string vazia e a só com espaços também são ausentes, e isso não é
 *parametricidade: `Number('')` é 0 e `Number('  ')` é 0, então sem esta guarda
 * o campo vazio de um CSV do IBGE — que a spec §4 e o foco de revisão 4 tratam
 * como "sem dado" — entraria como um zero de verdade. Medido antes da correção:
 * `geometriaBarras([{valor:''}])` devolvia `{ausente:false, valor:0, h:0}`, e
 * o resultado é um `rect` de altura zero — a barra mais baixa da escala pintada
 * como se fosse um valor medido. É a mesma mentira que o cinza de ausência
 * existe para evitar.
 *
 * A string é testada ANTES da conversão de propósito: depois de `Number`, "" e
 * "  " já viraram o mesmo 0, e a distinção se perde.
 */
export function valorPresente(v) {
    if (v === null || v === undefined) return false;
    if (typeof v === 'boolean') return false;
    if (typeof v === 'string' && v.trim() === '') return false;
    const n = Number(v);
    if (!Number.isFinite(n)) return false;
    return n >= 0;
}

/* ------------------------------------------------------------------ */
/* Funções puras: série                                                 */
/* ------------------------------------------------------------------ */

/**
 * Quebra uma série em segmentos contínuos.
 *
 * Recebe pontos `{r: ano, v: valor}` e devolve uma lista de segmentos; cada
 * segmento é a lista dos pontos que se conectam por linha. Um ponto é
 * quebrador quando:
 *   - não tem valor (`null`, `undefined`, `-1` — a sentinela do aggregate);
 *   - o ano não é exatamente o anterior + 1, ou seja, falta um ano no meio;
 *   - o ano é anterior ao anterior (série fora de ordem).
 *
 * Um ponto isolado (2021 e 2024 sem nada entre) vira um segmento de um
 * ponto, e não desaparece: o brief da Tarefa 10 mede "2 segmentos separados,
 * e não 1" justamente para isso, e o desenho trata o segmento curto como um
 * marcador — o ano que existe é mostrado, não some da tela.
 *
 * Devolve `[]` para lista vazia, e nunca um segmento com zero pontos.
 */
export function segmentosLinha(pontos) {
    const segmentos = [];
    let atual = [];
    let anoAnterior = null;

    const fecha = () => {
        if (atual.length > 0) segmentos.push(atual);
        atual = [];
    };

    for (const p of pontos || []) {
        const ano = p && typeof p.r === 'number' ? p.r : parseInt(p && p.r, 10);
        const valor = p ? p.v : null;

        if (!Number.isInteger(ano)) {
            /* Ano ilegível: fecha o que estava aberto e segue. */
            fecha();
            anoAnterior = null;
            continue;
        }

        const semValor = !valorPresente(valor);
        const descontinuo = anoAnterior !== null && ano !== anoAnterior + 1;

        if (semValor || descontinuo) {
            fecha();
        }

        if (!semValor) {
            atual.push({ r: ano, v: valor });
            anoAnterior = ano;
        } else {
            anoAnterior = null;
        }
    }

    fecha();
    return segmentos;
}

/**
 * Casa cada valor da série com o ano dele.
 *
 * A resposta do servidor traz `serie` (os valores, na ordem) e `serie_anos`
 * (os anos, na mesma ordem). Os dois são obrigatórios: sem o ano, o único
 * caminho seria tratar a posição como ano, e 2007, 2010, 2022 e 2023
 * desapareceriam da série sem o eixo declarar a lacuna.
 *
 * Um `null` dentro de `serie` é uma lacuna **declarada** — o ano existe e o
 * valor não. O ponto entra com `v: null` para que o corte caia na posição
 * certa; quem decide o que fazer com ele é `segmentosLinha`.
 */
export function paresDaSerie(resposta) {
    const serie = resposta && Array.isArray(resposta.serie) ? resposta.serie : null;
    const anos = resposta && Array.isArray(resposta.serie_anos) ? resposta.serie_anos : null;
    if (!serie || !anos) return [];
    const saida = [];
    const n = Math.min(serie.length, anos.length);
    for (let i = 0; i < n; i++) {
        const ano = typeof anos[i] === 'number' ? anos[i] : parseInt(anos[i], 10);
        if (!Number.isInteger(ano)) continue;
        saida.push({ r: ano, v: serie[i] });
    }
    return saida;
}

/**
 * Constrói uma função de escala linear de domínio [d0,d1] para a faixa
 * [r0,r1].
 *
 * Domínio degenerado (d0 === d1, ou lista vazia com mínimo e máximo iguais)
 * não divide por zero: devolve uma função estável em vez de devolver NaN,
 * que apareceria no atributo `d` do path e apagaria o gráfico inteiro sem
 * mensagem. Nesse caso o valor cai no meio da faixa, que é a leitura honesta:
 * com um único valor, qualquer altura é arbitrária.
 */
export function escalaLinear(d0, d1, r0, r1) {
    const ini = Number.isFinite(d0) ? d0 : 0;
    const fim = Number.isFinite(d1) ? d1 : 0;
    const base = Number.isFinite(r0) ? r0 : 0;
    const topo = Number.isFinite(r1) ? r1 : 0;
    const meio = base + (topo - base) / 2;

    if (fim === ini) return () => meio;

    return (v) => {
        const n = Number.isFinite(v) ? v : ini;
        const t = (n - ini) / (fim - ini);
        return base + t * (topo - base);
    };
}

/**
 * Domínio de valores de uma lista de pontos, ignorando ausentes.
 *
 * Devolve `{min, max}` numérico. Com lista vazia devolve `null` — e não
 * `{min:0,max:0}` — para que o chamador possa distinguir "não há dado" de
 * "todos os valores são zero".
 */
export function dominioValores(pontos) {
    let min = null;
    let max = null;
    for (const p of pontos || []) {
        if (!p || !valorPresente(p.v)) continue;
        const v = Number(p.v);
        if (min === null || v < min) min = v;
        if (max === null || v > max) max = v;
    }
    if (min === null) return null;
    return { min, max };
}

/**
 * Fatias de donut a partir de rótulo/valor.
 *
 * Lista vazia devolve `[]` em vez de um `d` com NaN: um `d` inválido faz o
 * navegador recusar o path inteiro, e o usuário veria um buraco no lugar do
 * gráfico sem nenhuma pista do motivo.
 */
export function fatiasDonut(itens, total) {
    const valorDe = (i) => (i && (i.v !== undefined ? i.v : i.valor));
    const base = Number.isFinite(total) && total > 0
        ? total
        : (itens || []).reduce((s, i) => {
            const v = valorDe(i);
            return s + (Number.isFinite(v) && v > 0 ? v : 0);
        }, 0);
    if (!(base > 0)) return [];
    const out = [];
    let acumulado = 0;
    for (const item of itens || []) {
        const cru = valorDe(item);
        const v = Number.isFinite(cru) && cru > 0 ? cru : 0;
        const fatia = (v / base) * 360;
        out.push({ rotulo: item && item.rotulo, valor: v, inicio: acumulado, tamanho: fatia });
        acumulado += fatia;
    }
    return out;
}

/**
 * As duas fatias que a spec §6 pede que fiquem visíveis: quantos têm dado e
 * quantos não têm.
 *
 * `com_dado` ausente vira **zero**, nunca `total`: com o KPI faltando, dizer
 * que todos os 5.570 municípios têm dado publicado é inventar o número mais
 * visível da tela. A fatia "sem dado" absorve o total inteiro, e o painel
 * fica inteiro marcado como "sem dado" — que é o estado real do dado, não uma
 * leitura otimista dele.
 *
 * `unidade` entra no rótulo porque o mesmo par de fatias é desenhado para
 * município e para estado, e "Municípios com dado" no recorte de estado seria
 * uma frase falsa na tela.
 */
export function publicacaoDoAno(total, comDado, unidade = 'Municípios') {
    const t = valorPresente(total) ? Number(total) : 0;
    if (!(t > 0)) return [];
    const c = valorPresente(comDado) ? Math.min(Number(comDado), t) : null;
    return [
        { rotulo: `${unidade} com dado publicado`, valor: c === null ? 0 : c },
        { rotulo: `${unidade} sem dado publicado`, valor: c === null ? t : t - c },
    ];
}

/**
 * Os anos que viram rótulo do eixo X.
 *
 * Não são anos quaisquer: são os **começos e fins de cada segmento**, que é
 * onde a informação está. Uma série contínua de 22 anos labelaria 7 anos
 * quaisquer; esta de 22 anos com quatro blocos labela as fronteiras, e o leitor
 * vê 2001–2006, 2008, 2011, 2021, 2024, 2026 — os cortes ficam explícitos sem
 * que nenhum ano que não foi publicado apareça.
 *
 * O espaçamento mínimo descarta o rótulo que atropelaria o anterior. Descartar
 * o *que vem depois* é o que preserva as fronteiras dos blocos, que são as que
 * importam.
 */
export function rotulosDoEixoX(segmentos, opcoes = {}) {
    const maximo = opcoes.maximo ?? 7;
    const espacoMinimo = opcoes.espacoMinimo ?? 34;
    const px = typeof opcoes.px === 'function' ? opcoes.px : null;

    const anos = [];
    for (const seg of segmentos || []) {
        if (!Array.isArray(seg) || seg.length === 0) continue;
        anos.push(seg[0].r);
        anos.push(seg[seg.length - 1].r);
    }
    const unicos = [...new Set(anos)].sort((a, b) => a - b);
    if (!px) return unicos.slice(0, maximo);

    const escolhidos = [];
    /* O nome é `xAnterior` porque o guarda de acento de tests/test_web.sh
     * varre o arquivo inteiro: um nome de variável sem acento vira falha de
     * português num arquivo que está todo em português. */
    let xAnterior = -Infinity;
    for (const ano of unicos) {
        if (escolhidos.length >= maximo) break;
        const x = px(ano);
        if (!Number.isFinite(x)) continue;
        if (x - xAnterior < espacoMinimo) continue;
        escolhidos.push(ano);
        xAnterior = x;
    }
    return escolhidos;
}

/** Os anos que faltam entre dois segmentos, em ordem. */
export function lacunasDeLinha(segmentos) {
    const lacunas = [];
    for (let i = 0; i + 1 < (segmentos || []).length; i++) {
        const anterior = segmentos[i];
        const seguinte = segmentos[i + 1];
        const de = anterior[anterior.length - 1].r;
        const ate = seguinte[0].r;
        if (ate <= de + 1) continue;
        lacunas.push({ de, ate, faltam: ate - de - 1, anos: anosEntre(de, ate) });
    }
    return lacunas;
}

function anosEntre(de, ate) {
    const anos = [];
    for (let a = de + 1; a < ate; a++) anos.push(a);
    return anos;
}

/* ------------------------------------------------------------------ */
/* Funções puras: cor                                                   */
/* ------------------------------------------------------------------ */

/** `n` cores distintas para as fatias, na ordem em que aparecem. */
export function coresCategoricas(n) {
    const total = Number.isFinite(n) && n > 0 ? Math.floor(n) : 0;
    const saida = [];
    for (let i = 0; i < total; i++) saida.push(PALETA[i % PALETA.length]);
    return saida;
}

/** Mistura dois extremos RGB. Só aritmética, nenhuma dependência. */
function misturaRgb(a, b, t) {
    const k = Math.min(Math.max(t, 0), 1);
    const canais = [a, b].map((cor) => [
        parseInt(cor.slice(1, 3), 16),
        parseInt(cor.slice(3, 5), 16),
        parseInt(cor.slice(5, 7), 16),
    ]);
    const hex = (n) => n.toString(16).padStart(2, '0');
    const saida = [0, 1, 2].map((i) =>
        Math.round(canais[0][i] + (canais[1][i] - canais[0][i]) * k));
    return `#${hex(saida[0])}${hex(saida[1])}${hex(saida[2])}`;
}

const RAMPA_A = '#1c3a5e';
const RAMPA_B = '#2fbf71';

/**
 * Cor de uma rampa contínua, de `t` em `[0,1]`.
 *
 * `t` fora da faixa é aparado no extremo, não extrapolado: um `t` de 1,2
 * inventaria uma cor que não está na rampa, e o gráfico mentiria sobre o
 * próprio domínio.
 */
export function corSequencial(t) {
    const v = Number.isFinite(t) ? t : 0;
    return misturaRgb(RAMPA_A, RAMPA_B, v);
}

/** Corte de um texto para caber numa faixa, sem quebrar acento no meio. */
export function encurtaTexto(texto, maximo) {
    const s = texto === null || texto === undefined ? '' : String(texto);
    if (!Number.isFinite(maximo) || maximo <= 1) return s;
    if (s.length <= maximo) return s;
    return `${s.slice(0, Math.max(1, maximo - 1)).trimEnd()}…`;
}

/**
 * Número compacto para os rótulos do eixo Y.
 *
 * `45.973.194` são onze caracteres numa faixa de 40 pixels. Em pt-BR o
 * encurtamento é "milhões" e "mil", que é como quem lê dado no Brasil lê.
 */
export function abreviaNumero(v) {
    if (!valorPresente(v)) return '--';
    const n = Number(v);
    const a = Math.abs(n);
    const fmt = (x, d) => x.toLocaleString('pt-BR', { maximumFractionDigits: d });
    if (a >= 1e9) return `${fmt(n / 1e9, 1)} bi`;
    if (a >= 1e6) return `${fmt(n / 1e6, 1)} mi`;
    if (a >= 1e3) return `${fmt(n / 1e3, 0)} mil`;
    return fmt(n, 2);
}

/* ------------------------------------------------------------------ */
/* Funções puras: geometria                                             */
/* ------------------------------------------------------------------ */

const LARGURA_PADRAO = 640;
const ALTURA_LINHA = 220;
const ALTURA_BARRAS = 200;
const TAMANHO_DONUT = 160;
const MARGEM = 28;
const ESPESSURA_DONUT = 26;

/**
 * Geometria da linha do tempo.
 *
 * A escala Y vai do menor ao maior valor **com dado**, e não do zero: a
 * variação de uma população em 25 anos é de uns 20%, e um eixo que começa em
 * zero transformaria essa variação em uma reta. É a convenção correta para
 * série temporal, desde que os rótulos do eixo mostrem os valores — e eles
 * mostram.
 */
export function geometriaLinha(pontos, opcoes = {}) {
    const largura = opcoes.largura ?? LARGURA_PADRAO;
    const altura = opcoes.altura ?? ALTURA_LINHA;
    const margem = opcoes.margem ?? MARGEM;
    const base = altura - margem;

    const segmentos = segmentosLinha(pontos);
    const anos = (pontos || [])
        .map((p) => (p ? parseInt(p.r, 10) : null))
        .filter((r) => Number.isInteger(r));
    const anoMin = anos.length ? Math.min(...anos) : 0;
    const anoMax = anos.length ? Math.max(...anos) : 0;

    const px = escalaLinear(anoMin, anoMax, margem, largura - margem);
    const dom = dominioValores(pontos);
    const py = escalaLinear(dom ? dom.min : 0, dom ? dom.max : 1, base, margem);

    return {
        largura,
        altura,
        margem,
        base,
        escalaX: px,
        escalaY: py,
        anos: { min: anoMin, max: anoMax },
        dominio: dom,
        vazio: segmentos.length === 0,
        segmentos: segmentos.map((seg) => ({
            pontos: seg.map((p) => ({ x: px(p.r), y: py(p.v), ano: p.r, valor: p.v })),
        })),
        lacunas: lacunasDeLinha(segmentos).map((l) => ({
            ...l, x1: px(l.de), x2: px(l.ate), x: (px(l.de) + px(l.ate)) / 2,
        })),
        rotulos: rotulosDoEixoX(segmentos, { px, maximo: opcoes.rotulos ?? 7 }),
    };
}

/**
 * Geometria das barras.
 *
 * Duas regras que não são estética:
 *
 *   - O eixo Y começa em **zero**. Barra truncada mente sobre a proporção, e
 *     a proporção é a leitura principal de uma barra.
 *   - Uma entrada sem valor vira `{ausente: true}` e **sem** `h`. Uma barra
 *     de altura zero e uma barra de valor zero são a mesma coisa na tela: as
 *     duas diriam "zero". O ausente precisa aparecer como ausência, com um
 *     traço de "sem dado" no lugar.
 */
export function geometriaBarras(itens, opcoes = {}) {
    const largura = opcoes.largura ?? LARGURA_PADRAO;
    const altura = opcoes.altura ?? ALTURA_BARRAS;
    const margem = opcoes.margem ?? MARGEM;
    const base = altura - margem;
    const maximo = Number.isFinite(opcoes.maximo) && opcoes.maximo > 0
        ? Math.floor(opcoes.maximo) : 10;

    /* Normaliza e ordena. `Array.sort` é estável no V8, então fatias com valor
     * ausente mantêm entre si a ordem da resposta, que é a ordem do servidor. */
    const normalizados = (itens || [])
        .filter((it) => it !== null && it !== undefined)
        .map((it) => ({ rotulo: it.rotulo !== undefined ? it.rotulo : it.nome, valor: it.valor }))
        .sort((a, b) => {
            const va = valorPresente(a.valor) ? Number(a.valor) : -1;
            const vb = valorPresente(b.valor) ? Number(b.valor) : -1;
            return vb - va;
        })
        .slice(0, maximo);

    let maior = 0;
    for (const it of normalizados) {
        if (valorPresente(it.valor)) maior = Math.max(maior, Number(it.valor));
    }

    const py = escalaLinear(0, maior, base, margem);
    const vao = normalizados.length ? (largura - 2 * margem) / normalizados.length : 0;
    const larguraBarra = vao > 0 ? Math.max(2, vao * 0.66) : 0;
    const alturaMaxRotulo = opcoes.altura ?? ALTURA_BARRAS;
    const cabemRotulos = vao >= 52;
    const cabemValores = vao >= 44;

    const barras = normalizados.map((it, i) => {
        const ausente = !valorPresente(it.valor);
        const x = margem + vao * i + (vao - larguraBarra) / 2;
        const baseComum = {
            rotulo: it.rotulo === null || it.rotulo === undefined || it.rotulo === ''
                ? '--' : String(it.rotulo),
            valor: ausente ? null : Number(it.valor),
            ausente,
            x,
            base,
            largura: larguraBarra,
            vao,
        };
        if (ausente) return baseComum;
        /* `maior === 0` significa que todo valor presente é zero: aí a barra
         * tem altura zero de verdade. Deixar a escala degenerada devolver o
         * meio da faixa daria altura máxima para um valor zero. */
        const h = maior > 0 ? base - py(Number(it.valor)) : 0;
        return { ...baseComum, h, y: base - h };
    });

    return {
        largura,
        altura: alturaMaxRotulo,
        margem,
        base,
        vazio: barras.length === 0,
        dominio: { min: 0, max: maior },
        escalaY: py,
        vao,
        rotuloMaximo: cabemRotulos ? Math.max(6, Math.floor(vao / 6.5)) : 0,
        valorMaximo: cabemValores ? Math.max(6, Math.floor(vao / 7.2)) : 0,
        barras,
        /* Escala do eixo Y em quatro degraus: a altura do maior valor tem de
         * aparecer como número, não só como comprimento de barra. */
        ticks: quatroTicks(0, maior),
    };
}

/** Quatro degraus de uma escala de valor, arredondados para número legível. */
export function quatroTicks(min, max) {
    const a = valorPresente(min) ? Number(min) : 0;
    const b = valorPresente(max) ? Number(max) : 0;
    if (b <= a) return [a];
    return [0, 1, 2, 3, 4].map((i) => a + ((b - a) * i) / 4);
}

/**
 * Geometria do anel.
 *
 * Uma fatia de tamanho zero não entra em `arcos`. Desenhar um arco de 0° tem
 * a flag de arco grande ambígua: o navegador fecha o arco do lado que lhe
 * convém e pinta um círculo inteiro. Em 2024 os 5.570 municípios têm dado, a
 * fatia "sem dado" é 0%, e o círculo inteiro diria que existe gente sem dado.
 *
 * Uma fatia de 360° é o mesmo problema pelo outro lado, e sai como dois arcos
 * de 180°.
 */
export function geometriaDonut(fatias, opcoes = {}) {
    const tamanho = opcoes.tamanho ?? TAMANHO_DONUT;
    const espessura = opcoes.espessura ?? ESPESSURA_DONUT;
    const cx = tamanho / 2;
    const cy = tamanho / 2;
    const raio = Math.max(1, tamanho / 2 - espessura / 2 - 1);

    const arcos = [];
    for (const f of fatias || []) {
        const tamanhoFatia = Number(f && f.tamanho);
        if (!Number.isFinite(tamanhoFatia) || tamanhoFatia <= 0) continue;
        if (tamanhoFatia >= 359.999) {
            /* Inteiro: dois meios, porque um `A` de 360° começa e termina no
             * mesmo ponto e o navegador não sabe traçar o caminho. */
            arcos.push({
                d: `${arco(cx, cy, raio, f.inicio, f.inicio + 180)} ${arco(cx, cy, raio, f.inicio + 180, f.inicio + 359.999)}`,
                rotulo: f.rotulo, valor: f.valor, tamanho: tamanhoFatia, inteiro: true,
            });
            continue;
        }
        arcos.push({
            d: arco(cx, cy, raio, f.inicio, f.inicio + tamanhoFatia),
            rotulo: f.rotulo, valor: f.valor, tamanho: tamanhoFatia, inteiro: false,
        });
    }

    return {
        tamanho, espessura, raio,
        anel: { cx, cy, raio, espessura },
        arcos,
        vazio: arcos.length === 0,
        cores: coresCategoricas(arcos.length),
    };
}

/** Caminho de um arco, em graus, com 0° no topo e sentido horário. */
export function arco(cx, cy, raio, inicioGraus, fimGraus) {
    const a0 = ((inicioGraus - 90) * Math.PI) / 180;
    const a1 = ((fimGraus - 90) * Math.PI) / 180;
    const x0 = cx + raio * Math.cos(a0);
    const y0 = cy + raio * Math.sin(a0);
    const x1 = cx + raio * Math.cos(a1);
    const y1 = cy + raio * Math.sin(a1);
    const grande = fimGraus - inicioGraus > 180 ? 1 : 0;
    return `M${x0.toFixed(3)},${y0.toFixed(3)} A${raio},${raio} 0 ${grande} 1 ${x1.toFixed(3)},${y1.toFixed(3)}`;
}

/* ------------------------------------------------------------------ */
/* Daqui para baixo: DOM, via doc injetado.                            */
/* ------------------------------------------------------------------ */

function elemento(doc, tag, texto) {
    const el = doc.createElement(tag);
    if (texto !== undefined) el.textContent = texto;
    return el;
}

/** Nó SVG no namespace certo — `createElement` comum não serve para SVG. */
function svg(doc, tag, atributos) {
    const el = doc.createElementNS('http://www.w3.org/2000/svg', tag);
    for (const k of Object.keys(atributos || {})) {
        const v = atributos[k];
        if (v === null || v === undefined) continue;
        el.setAttribute(k, String(v));
    }
    return el;
}

/** Texto de dentro de um nó SVG. */
function rotuloSvg(doc, tag, atributos, texto) {
    const el = svg(doc, tag, atributos);
    el.textContent = texto === undefined ? '' : texto;
    return el;
}

function limpa(el) {
    while (el.firstChild) el.removeChild(el.firstChild);
    return el;
}

function aviso(doc, el, texto) {
    limpa(el);
    el.appendChild(elemento(doc, 'p', texto));
    el.className = 'grafico-vazio';
    return el;
}

function moldura(doc, largura, altura, titulo, classe) {
    return svg(doc, 'svg', {
        viewBox: `0 0 ${largura} ${altura}`,
        width: '100%',
        height: altura,
        preserveAspectRatio: 'xMidYMid meet',
        role: 'img',
        'aria-label': titulo || 'Gráfico',
        class: classe || 'grafico-svg',
    });
}

/**
 * Desenha a linha do tempo em `el`, a partir dos segmentos já calculados.
 *
 * Só converte números em coordenadas. Se um gráfico quebrar daqui, a culpa é
 * de `geometriaLinha`, que é testável em node sem DOM.
 */
export function desenharLinha(doc, el, pontos, opcoes = {}) {
    limpa(el);
    const g = geometriaLinha(pontos, opcoes);

    if (g.vazio) {
        return aviso(doc, el, opcoes.semSerie || 'Sem série para desenhar neste recorte.');
    }

    const svgEl = moldura(doc, g.largura, g.altura, opcoes.titulo || 'Série temporal');
    const cor = opcoes.cor || 'var(--acento)';

    /* Grade do eixo Y, com os valores: é ela que diz que a escala não começa
     * em zero, e é por isso que ela fica visível. */
    for (const t of quatroTicks(g.dominio.min, g.dominio.max)) {
        const y = g.escalaY(t);
        svgEl.appendChild(svg(doc, 'line', {
            x1: g.margem, x2: g.largura - g.margem, y1: y.toFixed(2), y2: y.toFixed(2),
            class: 'grade',
        }));
        svgEl.appendChild(rotuloSvg(doc, 'text', {
            x: g.margem - 4, y: (y + 3).toFixed(2), 'text-anchor': 'end', class: 'eixo-valor',
        }, abreviaNumero(t)));
    }

    /* A lacuna, desenhada. Um vão sem marca é um vão que o olho completa por
     * conta própria, e é a interpolação que o olho completa. */
    for (const lac of g.lacunas) {
        const traco = svg(doc, 'line', {
            x1: lac.x.toFixed(2), x2: lac.x.toFixed(2),
            y1: g.margem, y2: g.base.toFixed(2),
            class: 'lacuna', 'data-lacuna': `${lac.de}-${lac.ate}`,
        });
        traco.appendChild(rotuloSvg(doc, 'title', {},
            `${lac.anos.join(', ')}: sem dado publicado`));
        svgEl.appendChild(traco);
    }

    for (const seg of g.segmentos) {
        if (seg.pontos.length >= 2) {
            const d = seg.pontos
                .map((p, i) => `${i === 0 ? 'M' : 'L'}${p.x.toFixed(2)},${p.y.toFixed(2)}`)
                .join(' ');
            const caminho = svg(doc, 'path', {
                d,
                fill: 'none',
                stroke: cor,
                'stroke-width': 2,
                'stroke-linejoin': 'round',
                'stroke-linecap': 'round',
                class: 'linha-serie',
            });
            caminho.appendChild(rotuloSvg(doc, 'title', {},
                seg.pontos.map((p) => `${p.ano}: ${textoOpcoes(p.valor)}`).join(' · ')));
            svgEl.appendChild(caminho);
        } else {
            /* Ano isolado: um ponto não forma linha, mas o dado existe e
             * precisa aparecer. Vira um marcador. */
            const p = seg.pontos[0];
            svgEl.appendChild(svg(doc, 'circle', {
                cx: p.x.toFixed(2), cy: p.y.toFixed(2), r: 3, fill: cor, class: 'ponto-serie',
            }));
        }
    }

    for (const ano of g.rotulos) {
        svgEl.appendChild(rotuloSvg(doc, 'text', {
            x: g.escalaX(ano).toFixed(2), y: (g.base + 14).toFixed(2),
            'text-anchor': 'middle', class: 'eixo-ano',
        }, String(ano)));
    }

    el.appendChild(svgEl);
    el.className = '';
    return el;
}

/** Desenha as barras de `itens`, com o traço de "sem dado" no lugar do vazio. */
export function desenharBarras(doc, el, itens, opcoes = {}) {
    limpa(el);
    const g = geometriaBarras(itens, opcoes);

    if (g.vazio) {
        return aviso(doc, el, opcoes.semDados || 'Sem dado para desenhar neste recorte.');
    }

    const svgEl = moldura(doc, g.largura, g.altura, opcoes.titulo || 'Comparação de valores');
    const cores = coresCategoricas(g.barras.length);
    const semDado = coresCategoricas(1)[0];

    for (const t of g.ticks) {
        const y = g.escalaY(t);
        svgEl.appendChild(svg(doc, 'line', {
            x1: g.margem, x2: g.largura - g.margem, y1: y.toFixed(2), y2: y.toFixed(2),
            class: 'grade',
        }));
        svgEl.appendChild(rotuloSvg(doc, 'text', {
            x: g.margem - 4, y: (y + 3).toFixed(2), 'text-anchor': 'end', class: 'eixo-valor',
        }, abreviaNumero(t)));
    }

    g.barras.forEach((b, i) => {
        if (b.ausente) {
            const traco = svg(doc, 'line', {
                x1: b.x.toFixed(2), x2: (b.x + b.largura).toFixed(2),
                y1: b.base.toFixed(2), y2: b.base.toFixed(2),
                class: 'barra-ausente', 'data-ausente': 'true', stroke: COR_AUSENCIA,
            });
            traco.appendChild(rotuloSvg(doc, 'title', {}, `${b.rotulo}: sem dado publicado`));
            svgEl.appendChild(traco);
            if (g.rotuloMaximo > 0) {
                svgEl.appendChild(rotuloSvg(doc, 'text', {
                    x: (b.x + b.largura / 2).toFixed(2), y: (b.base - 5).toFixed(2),
                    'text-anchor': 'middle', class: 'sem-dado', fill: COR_AUSENCIA,
                }, 'sem dado'));
            }
            return;
        }

        svgEl.appendChild(svg(doc, 'rect', {
            x: b.x.toFixed(2), y: b.y.toFixed(2),
            width: b.largura.toFixed(2), height: b.h.toFixed(2),
            fill: opcoes.cor ? 'var(--acento)' : (cores[i] || semDado),
            class: 'barra',
        }));
        if (g.valorMaximo > 0) {
            svgEl.appendChild(rotuloSvg(doc, 'text', {
                x: (b.x + b.largura / 2).toFixed(2), y: (b.y - 4).toFixed(2),
                'text-anchor': 'middle', class: 'barra-valor',
            }, textoOpcoes(b.valor)));
        }
    });

    g.barras.forEach((b) => {
        if (g.rotuloMaximo > 0) {
            svgEl.appendChild(rotuloSvg(doc, 'text', {
                x: (b.x + b.largura / 2).toFixed(2), y: (g.base + 13).toFixed(2),
                'text-anchor': 'middle', class: 'eixo-ano',
            }, encurtaTexto(b.rotulo, g.rotuloMaximo)));
        }
    });

    el.appendChild(svgEl);
    el.className = '';
    return el;
}

/** Desenha o anel e a legenda. Sem fatia não há arco, e a ausência é dita. */
export function desenharDonut(doc, el, fatias, opcoes = {}) {
    limpa(el);
    const g = geometriaDonut(fatias, opcoes);

    if (g.vazio) {
        return aviso(doc, el, opcoes.semDados
            || 'Sem dado para desenhar: nenhum valor neste recorte.');
    }

    const total = (fatias || []).reduce((s, f) => s + (f && f.valor > 0 ? f.valor : 0), 0);
    /* `grafico-svg-anelo` existe para o CSS não esticar o anel para a largura
     * inteira: um donut de mil pixels de largura não é donut, é uma pizza. */
    const svgEl = moldura(doc, g.tamanho, g.tamanho, opcoes.titulo || 'Composição',
        'grafico-svg grafico-svg-anelo');
    svgEl.appendChild(svg(doc, 'circle', {
        cx: g.anel.cx, cy: g.anel.cy, r: g.anel.raio,
        fill: 'none', stroke: COR_AUSENCIA, 'stroke-width': 0.5, class: 'anel-fundo',
    }));

    g.arcos.forEach((a, i) => {
        const cor = g.cores[i] || COR_AUSENCIA;
        svgEl.appendChild(svg(doc, 'path', {
            d: a.d, fill: 'none', stroke: cor,
            'stroke-width': g.anel.espessura, class: 'anel-fatia',
        }));
    });

    el.appendChild(svgEl);
    el.appendChild(legenda(doc, g.arcos, total));
    el.className = '';
    return el;
}

/** Lista de texto com a cor e a participação de cada fatia. */
function legenda(doc, arcos, total) {
    const ul = elemento(doc, 'ul', undefined);
    ul.className = 'legenda';
    for (const a of arcos) {
        const li = elemento(doc, 'li');
        const marca = elemento(doc, 'span');
        marca.className = 'legenda-cor';
        /* A cor vai por atributo, e não por estilo: o front não monta CSS
         * inline, e é o atributo que sobrevive ao `innerHTML = ''` da
         * próxima consulta. */
        marca.setAttribute('data-cor', a.cor || COR_AUSENCIA);
        li.appendChild(marca);
        li.appendChild(elemento(doc, 'span', `${a.rotulo}: ${textoOpcoes(a.valor)}`
            + ` (${percentual(a.valor, total)})`));
        ul.appendChild(li);
    }
    return ul;
}

function percentual(valor, total) {
    if (!total || total <= 0) return '--';
    const p = (valor / total) * 100;
    return `${p.toLocaleString('pt-BR', { maximumFractionDigits: 1 })}%`;
}

/**
 * Escreve um cartão de KPI.
 *
 * `el` nulo cria o cartão; com `el` dado, escreve dentro do que já existe e
 * cria as peças que faltarem. O valor ausente vira `--` por `textoOpcoes`, que
 * é a mesma função que a tabela e os rótulos usam — uma segunda implementação
 * seria uma segunda chance de escrever 0 onde não há dado.
 */
export function desenharKpi(doc, el, dados = {}) {
    const d = dados || {};
    const cartao = el || elemento(doc, 'div');
    if (!el) cartao.className = 'kpi';

    let rotulo = cartao.querySelector ? cartao.querySelector('.kpi-rotulo') : null;
    let valor = cartao.querySelector ? cartao.querySelector('.kpi-valor') : null;
    if (!rotulo) {
        rotulo = elemento(doc, 'span');
        rotulo.className = 'kpi-rotulo';
        cartao.appendChild(rotulo);
    }
    if (!valor) {
        valor = elemento(doc, 'span');
        valor.className = 'kpi-valor';
        cartao.appendChild(valor);
    }

    rotulo.textContent = d.rotulo || '';
    valor.textContent = textoOpcoes(d.valor);
    if (d.unidade) valor.appendChild(doc.createTextNode(` ${d.unidade}`));
    return cartao;
}

/* ------------------------------------------------------------------ */
/* Contrato do brief: o documento vem das opções                       */
/* ------------------------------------------------------------------ */

/**
 * `opcoes.doc` é o documento; sem ele, o global — que existe só no navegador.
 * Os testes sempre passam o `doc`, e por isso rodam em node sem jsdom.
 */
function docDe(opcoes) {
    const doc = opcoes && opcoes.doc;
    if (doc) return doc;
    if (typeof document !== 'undefined') return document;
    throw new Error('desenhar gráfico precisa de um documento: passe { doc }');
}

export function linha(el, pontos, opcoes = {}) {
    return desenharLinha(docDe(opcoes), el, pontos, opcoes);
}

export function barras(el, itens, opcoes = {}) {
    return desenharBarras(docDe(opcoes), el, itens, opcoes);
}

export function donut(el, fatias, opcoes = {}) {
    return desenharDonut(docDe(opcoes), el, fatias, opcoes);
}

export function kpi(el, dados = {}, opcoes = {}) {
    return desenharKpi(docDe(opcoes), el, dados);
}