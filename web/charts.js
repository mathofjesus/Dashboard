/* web/charts.js — cálculo dos gráficos, separado do desenho.
 *
 * Ruling C-5, aplicado já aqui: tudo que é *cálculo* neste arquivo é função
 * pura, sem `document`, sem `window`. É o que permite rodar os testes em node
 * (`node tests/test_web.mjs`) sem jsdom, que não é instalável aqui.
 *
 * A função que importa para o resto do front é `segmentosLinha`: é dela que
 * sai o fato mais importante da visualização, que a série histórica do IBGE
 * tem lacuna em 2022 e 2023 e essa lacuna precisa aparecer como um corte no
 * gráfico. Ligar 2021 direto em 2024 afirmaria que a série é contínua, e ela
 * não é — mostraria um crescimento inventado.
 *
 * As funções `desenhar*` recebem um `doc` explícito em vez de usar o global.
 * O DOM entra como argumento, nunca como pressuposto do módulo.
 */

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

        const semValor = valor === null || valor === undefined || Number.isNaN(valor) || valor < 0;
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
        const v = p ? p.v : null;
        if (v === null || v === undefined || Number.isNaN(v) || v < 0) continue;
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
    const base = Number.isFinite(total) && total > 0
        ? total
        : (itens || []).reduce((s, i) => s + (Number.isFinite(i && i.v) && i.v > 0 ? i.v : 0), 0);
    if (!(base > 0)) return [];
    const out = [];
    let acumulado = 0;
    for (const item of itens || []) {
        const v = item && Number.isFinite(item.v) && item.v > 0 ? item.v : 0;
        const fatia = (v / base) * 360;
        out.push({ rotulo: item && item.rotulo, valor: v, inicio: acumulado, tamanho: fatia });
        acumulado += fatia;
    }
    return out;
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

/**
 * Desenha a linha do gráfico em `el`, a partir dos segmentos já calculados.
 *
 * Só converte números em coordenadas. Se um gráfico quebrar daqui, a culpa é
 * de `segmentosLinha`, que é testável em node.
 */
export function desenharLinha(doc, el, pontos, opcoes = {}) {
    const largura = opcoes.largura ?? 640;
    const altura = opcoes.altura ?? 220;
    const margem = opcoes.margem ?? 28;
    const segmentos = segmentosLinha(pontos);
    const dom = dominioValores(pontos);

    while (el.firstChild) el.removeChild(el.firstChild);
    if (!segmentos.length) {
        el.appendChild(elemento(doc, 'p', 'Sem série para desenhar'));
        return el;
    }

    const anos = (pontos || []).map((p) => p && p.r).filter((r) => Number.isInteger(r));
    const anoMin = Math.min(...anos);
    const anoMax = Math.max(...anos);

    const px = escalaLinear(anoMin, anoMax, margem, largura - margem);
    const py = escalaLinear(dom.min, dom.max, altura - margem, margem);

    const svgEl = svg(doc, 'svg', {
        viewBox: `0 0 ${largura} ${altura}`,
        width: '100%',
        height: altura,
        role: 'img',
        'aria-label': opcoes.titulo || 'Série temporal',
    });

    for (const seg of segmentos) {
        if (seg.length >= 2) {
            const d = seg
                .map((p, i) => `${i === 0 ? 'M' : 'L'}${px(p.r).toFixed(2)},${py(p.v).toFixed(2)}`)
                .join(' ');
            svgEl.appendChild(svg(doc, 'path', {
                d,
                fill: 'none',
                stroke: opcoes.cor || 'currentColor',
                'stroke-width': 2,
                'stroke-linejoin': 'round',
                'stroke-linecap': 'round',
            }));
        } else if (seg.length === 1) {
            /* Ano isolado: um ponto não forma linha, mas o dado existe e
             * precisa aparecer. Vira um marcador. */
            const p = seg[0];
            svgEl.appendChild(svg(doc, 'circle', {
                cx: px(p.r).toFixed(2),
                cy: py(p.v).toFixed(2),
                r: 3,
                fill: opcoes.cor || 'currentColor',
            }));
        }
    }

    el.appendChild(svgEl);
    return el;
}
