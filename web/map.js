/**
 * web/map.js — o coroplético.
 *
 * Recebe a malha que o servidor já serviu simplificada e os valores da
 * consulta, e devolve um SVG pintado por município. Duas partes, e a separação
 * é deliberada:
 *
 *   - `geometriaMapa` é PURA: transforma (geojson, valores) em viewBox,
 *     caminhos com `d` já projetado e a lista da legenda. Nada de DOM, o que
 *     a torna testável em node — sem jsdom, sem navegador.
 *   - `desenharMapa` é a casca: chama a de cima e converte o resultado em
 *     elementos SVG via `document.createElementNS`.
 *
 * É o mesmo corte que o ruling C-5 do plano tomou para os gráficos da
 * Tarefa 10, e pelo mesmo motivo: sem ele, testar exigiria um navegador.
 *
 * Unidade de coordenada
 * ---------------------
 * O que chega em `geojson` já vem em graus×1000 e sempre inteiro (ver
 * src/geo.h). Dividir por 1000 aqui dentro seria perda de precisão à toa: a
 * projeção é uma interpolação entre extremos, e ela não precisa de graus
 * reais, só de uma caixa. Por isso `projectaPonto` trabalha nas unidades
 * inteiras e devolve [0,1].
 *
 * Eixo y
 * ------
 * No GeoJSON, y cresce para o NORTE (latitude). No SVG, y cresce para BAIXO.
 * Ignorar isso desenha o Brasil de cabeça para baixo — e o teste da projeção
 * existe para pegar exatamente isso.
 *
 * Ausência
 * ---------
 * Município sem valor NÃO recebe uma cor da escala. Recebe a classe
 * `mapa-sem-dado`, que o styles.css resolve para `var(--ausencia)` — o token
 * que já existe para os dois temas. Atributo SVG não resolve `var()`, então a
 * cor vai por classe; e nenhum `fill` é escrito nesse <path>, para o CSS não
 * ser sobrescrito. O motivo de não ser a cor mais baixa da escala: as duas
 * coisas precisam ser distinguíveis na tela. Um município que tem 3 não é o
 * mesmo que um município sem registro, e pintar os dois com o tom mais claro
 * da rampa transforma "não sei" em "sei que é pouco".
 */

/* `valorPresente` decide o que é ausente e `abreviaNumero` formata o rótulo
 * da legenda. Os dois vêm do charts.js de propósito: a definição de ausente
 * precisa ser UMA só para os dois módulos, senão o mapa pinta de cinza o que o
 * gráfico trata como número. */
import { valorPresente, abreviaNumero } from './charts.js';

/**
 * Classe que marca <path> sem dado. O nome tem que casar com o
 * `.mapa-sem-dado` do styles.css.
 */
export const CLASSE_SEM_DADO = 'mapa-sem-dado';

/** Classe do rótulo de estado, desenhado a partir do centroide. */
export const CLASSE_ROTULO = 'mapa-rotulo';

/** SVG gerado. Alto e estreito, que é a proporção do Brasil. */
const LARGURA = 1000;
const ALTURA = 620;

/**
 * Rampa sequencial de 5 tons, do menor para o maior valor.
 *
 * Escolha com três restrições, todas visíveis na tela:
 *
 *  1. Nenhum tom é cinza. O cinza é a cor de ausência, e uma rampa que
 *     terminasse em cinza fundiria "valor baixo" com "sem dado" no extremo
 *     mais próximo — que é justamente o que o usuário olha primeiro.
 *  2. A rampa é verde porque o acento do dashboard é `--acento: #2fbf71`;
 *     o mapa não introduz uma segunda família cromática.
 *  3. Os dois extremos funcionam sobre `--superficie` escura (#161b22) e
 *     clara (#ffffff): o mais escuro ainda contrasta com o branco, e o mais
 *     claro ainda contrasta com o cartão escuro.
 */
const RAMPA = ['#14532d', '#166534', '#15803d', '#22a34a', '#4ade80'];

/** Quantas faixas a legenda mostra. O plano pede 5. */
const N_BANDAS = 5;

/* ------------------------------------------------------------------ */
/* valores                                                             */
/* ------------------------------------------------------------------ */

/**
 * Lê o valor de um código, em qualquer uma das formas que chegam.
 *
 * Reaproveita `valorPresente` do charts.js em vez de repetir a regra: os dois
 * módulos precisam concordar sobre o que é ausente, e duas definições de
 * "ausente" divergem na primeira consulta em que uma delas esquecer o `-1`,
 * que é a sentinela que o aggregate manda para "não existe".
 */
export function valorDoCodigo(valores, cod) {
    if (!valores || cod === undefined || cod === null) return null;
    const chave = String(cod);
    /* Aceita chave string (o que o JSON entrega) e chave number, porque um
     * chamador em JS pode ter montado o objeto com o código como número. */
    let v = valores[chave];
    if (v === undefined && !Object.prototype.hasOwnProperty.call(valores, chave)) {
        v = valores[Number(chave)];
    }
    if (v === undefined) return null;
    return valorPresente(v) ? Number(v) : null;
}

/**
 * As faixas da escala, do menor para o maior.
 *
 * Devolve `[]` para lista vazia: sem valor não há legenda de valores, e o
 * mapa inteiro é ausência. Devolve uma lista vazia — e não uma faixa com
 * NaN — porque `NaN.min` finitamente apareceria na legenda e sairia como
 * "NaN" na tela.
 *
 * Domínio degenerado (todos os valores iguais) não divide por zero: o
 * domínio é aberto em +1 e as faixas ficam dentro dele. Sem isso, um único
 * município com dado produziria cinco faixas de largura zero, todas com a
 * mesma cor de índice instável.
 */
export function bandasDaEscala(valores, n = N_BANDAS) {
    const nums = [];
    for (const v of valores || []) {
        if (valorPresente(v)) nums.push(Number(v));
    }
    if (nums.length === 0) return [];

    let min = Math.min(...nums);
    let max = Math.max(...nums);
    if (min === max) {
        /* Um valor só (ou todos iguais): abre o domínio de um lado para que a
         * largura da faixa não seja zero e a divisão não seja por zero. O +1
         * entra na escala do próprio valor, então continua pequeno perto de
         * um milhão e grande perto de zero — não há um número que sirva para
         * os dois casos sem distorcer a leitura de nenhum deles. */
        max = min + Math.max(1, Math.abs(min) * 0.01);
    }

    const total = Math.max(1, Math.floor(n));
    const largura = (max - min) / total;
    const faixas = [];
    for (let i = 0; i < total; i++) {
        faixas.push({
            min: min + largura * i,
            max: min + largura * (i + 1),
            cor: RAMPA[Math.min(i, RAMPA.length - 1)],
            classe: '',
        });
    }
    return faixas;
}

/**
 * Em que faixa cai um valor, e com que cor ele sai.
 *
 * Devolve sempre um objeto com `cor` e `classe`. Quem não tem valor recebe a
 * classe de ausência e `cor: null` — e `cor: null` é o que faz o desenho
 * NÃO escrever atributo `fill`, deixando a classe pintar.
 */
export function corDoCodigo(valores, cod, faixas) {
    const v = valorDoCodigo(valores, cod);
    if (v === null) return { valor: null, cor: null, classe: CLASSE_SEM_DADO };

    const lista = faixas && faixas.length ? faixas : bandasDaEscala([v]);
    let escolhida = lista[lista.length - 1];
    for (const f of lista) {
        if (v < f.max) { escolhida = f; break; }
    }
    return { valor: v, cor: escolhida.cor, classe: '' };
}

/* ------------------------------------------------------------------ */
/* projeção                                                             */
/* ------------------------------------------------------------------ */

/**
 * A caixa que contém todos os vértices, em unidades de graus×1000.
 *
 * Devolve [x0, y0, x1, y1]. Devolve a caixa de um ponto só, e a caixa nula
 * quando não há geometria — quem chama transforma isso em "não há mapa" em
 * vez de propagar NaN para o viewBox.
 */
export function caixaDaGeo(geojson) {
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    let achou = false;

    const marcas = (x, y) => {
        if (!Number.isFinite(x) || !Number.isFinite(y)) return;
        achou = true;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    };

    for (const f of featuresDe(geojson)) {
        for (const anel of aneisDe(f)) {
            for (const p of anel) {
                if (Array.isArray(p)) marcas(p[0], p[1]);
            }
        }
    }
    if (!achou) return [0, 0, 1, 1];
    /* Ponto único (ou todos numa linha só): a projeção dividiria por zero.
     * Uma，一步 de folga em cada eixo mantém o desenho visível em vez de
     * sumir. */
    if (x1 === x0) { x0 -= 1; x1 += 1; }
    if (y1 === y0) { y0 -= 1; y1 += 1; }
    return [x0, y0, x1, y1];
}

/**
 * Projeta um ponto para [0,1]×[0,1] dentro da caixa, com y invertido.
 *
 * Não recorta: um ponto fora da caixa sai fora de [0,1]. Recortar aqui
 * transformaria erro de geometria em desenho plausível, e a malha real já
 * vem consistêntê dentro da própria caixa.
 */
export function projectaPonto(ponto, caixa) {
    const [x0, y0, x1, y1] = caixa;
    const px = Array.isArray(ponto) ? ponto[0] : NaN;
    const py = Array.isArray(ponto) ? ponto[1] : NaN;
    if (!Number.isFinite(px) || !Number.isFinite(py)) return [0, 0];
    const u = (px - x0) / (x1 - x0);
    /* y invertido: latitude cresce para cima, coordenada de tela para baixo. */
    const v = 1 - (py - y0) / (y1 - y0);
    if (!Number.isFinite(u) || !Number.isFinite(v)) return [0, 0];
    return [u, v];
}

/**
 * O atributo `d` de um anel, já projetado e multiplicado pela largura do SVG.
 *
 * Devolve string vazia para anel vazio ou nulo — o desenho pula o sub-caminho
 * em vez de escrever "MNaNNaNZ", que no SVG é um path inválido que o
 * navegador descarta em silêncio, sem erro visível.
 *
 * Um vértice ilegível (NaN, null, string) é pulado: a geometria real não tem
 * nenhum, e quando tiver, um vértice a menos é melhor do que um caminho
 * inteiro rejeitado pelo navegador.
 */
export function caminhoDoAnel(anel, caixa) {
    if (!Array.isArray(anel) || anel.length === 0) return '';
    const partes = [];
    let aberto = false;
    for (const p of anel) {
        const [x, y] = Array.isArray(p)
            ? projectaPonto(p, caixa || [0, 0, 1, 1])
            : [NaN, NaN];
        if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
        const X = (x * LARGURA).toFixed(2);
        const Y = (y * ALTURA).toFixed(2);
        partes.push(`${aberto ? 'L' : 'M'}${X} ${Y}`);
        aberto = true;
    }
    if (!aberto) return '';
    partes.push('Z');
    return partes.join('');
}

/* ------------------------------------------------------------------ */
/* geometria (puro)                                                    */
/* ------------------------------------------------------------------ */

/** As features de uma FeatureCollection, ou lista vazia. */
function featuresDe(geojson) {
    if (!geojson) return [];
    if (Array.isArray(geojson)) return geojson.filter((f) => f && f.geometry);
    if (Array.isArray(geojson.features)) return geojson.features.filter((f) => f && f.geometry);
    return [];
}

/**
 * Todos os anéis de uma feature, seja Polygon ou MultiPolygon.
 *
 * Polygon é `anel`; MultiPolygon é `polígono[]`, cada um com seus anéis.
 * Tratar os dois no mesmo laço é o que impede 2773 dos 5570 municípios de
 * sumirem do mapa.
 */
function aneisDe(f) {
    const g = f && f.geometry;
    if (!g || !Array.isArray(g.coordinates)) return [];
    if (g.type === 'Polygon') return g.coordinates;
    if (g.type === 'MultiPolygon') {
        const out = [];
        for (const poli of g.coordinates) {
            if (Array.isArray(poli)) out.push(...poli);
        }
        return out;
    }
    return [];
}

/**
 * A geometria inteira do mapa, sem tocar em DOM.
 *
 * Devolve `{viewBox, caminhos, legenda}`. Cada caminho traz `codarea`, `d`,
 * `cor`, `classe` e `centroide` — o centroide porque o rótulo de estado sai
 * dele, e o app precisa dele para o clique mostrar nome e valor.
 */
export function geometriaMapa(geojson, valores, opcoes = {}) {
    const caixa = caixaDaGeo(geojson);
    const valoresPresentes = Object.values(valores || {}).filter((v) => valorPresente(v));
    const faixas = bandasDaEscala(valoresPresentes, opcoes.bandas || N_BANDAS);

    const caminhos = [];
    for (const f of featuresDe(geojson)) {
        const props = f.properties || {};
        const codarea = props.codarea === undefined ? null : String(props.codarea);
        const tinta = corDoCodigo(valores, codarea, faixas);

        const d = aneisDe(f)
            .map((anel) => caminhoDoAnel(anel, caixa))
            .filter(Boolean)
            .join('');
        if (!d) continue;

        caminhos.push({
            codarea,
            d,
            cor: tinta.cor,
            classe: tinta.classe,
            valor: tinta.valor,
            centroide: props.centroide || null,
        });
    }

    return {
        viewBox: `0 0 ${LARGURA} ${ALTURA}`,
        caminhos,
        legenda: montaLegenda(faixas, { titulo: opcoes.titulo, unidade: opcoes.unidade }),
    };
}

/**
 * A legenda: as faixas da escala e a faixa de ausência.
 *
 * A faixa de ausência vem por último e carrega `semDado: true`, e é ela que
 * usa a classe — a legenda e o mapa têm que pintar a ausência com a mesma
 * cor, senão a legenda contradiz o desenho.
 */
function montaLegenda(faixas, { titulo, unidade } = {}) {
    const itens = faixas.map((f, i) => ({
        min: f.min,
        max: f.max,
        cor: f.cor,
        classe: '',
        semDado: false,
        rotulo: rotuloFaixa(f, i, faixas.length, unidade),
    }));
    itens.push({
        min: null,
        max: null,
        cor: null,
        classe: CLASSE_SEM_DADO,
        semDado: true,
        rotulo: 'sem dado',
    });
    if (titulo) itens.titulo = titulo;
    return itens;
}

/** O texto de uma faixa: extremos arredondados, ou "—" no domínio aberto. */
function rotuloFaixa(f, i, total, unidade) {
    if (total <= 1) return abreviaNumero(f.min);
    if (i === 0) return `até ${abreviaNumero(f.max)}`;
    if (i === total - 1) return `${abreviaNumero(f.min)}+`;
    return `${abreviaNumero(f.min)}–${abreviaNumero(f.max)}`;
}

/* ------------------------------------------------------------------ */
/* desenho                                                             */
/* ------------------------------------------------------------------ */

/**
 * Desenha o mapa em `el`.
 *
 * `el` é o elemento que recebe o SVG (o `<div id="mapa">` do index.html).
 * `doc` é o documento, por parâmetro: `map.js` não toca no global `document`
 * no escopo de módulo, e é isso que permite importar este arquivo em node.
 *
 * Devolve `{svg, geometria}` para quem quiser manipular depois, e nunca lança
 * por dado ruim: um mapa ausente é um cartão com o aviso, não uma tela em
 * branco (spec §12).
 */
export function desenharMapa(el, geojson, valores, opcoes = {}) {
    const doc = opcoes.doc || (el && el.ownerDocument) || null;
    if (!el || !doc) return null;

    const g = geometriaMapa(geojson, valores, opcoes);
    const NS = 'http://www.w3.org/2000/svg';

    el.replaceChildren();

    if (!g.caminhos.length) {
        return { svg: null, geometria: g, vazio: true };
    }

    const svg = doc.createElementNS(NS, 'svg');
    svg.setAttribute('viewBox', g.viewBox);
    svg.setAttribute('class', 'mapa-svg');
    svg.setAttribute('role', 'img');
    svg.setAttribute('preserveAspectRatio', 'xMidYMid meet');

    const rotulos = opcoes.rotulos !== false && g.caminhos.length <= 60;

    for (const c of g.caminhos) {
        const p = doc.createElementNS(NS, 'path');
        p.setAttribute('d', c.d);
        /* A ausência é pintada pela classe, e nenhum `fill` é escrito aqui:
         * um atributo precedence sobre a regra do CSS e o mapa voltaria a
         * mentir. */
        if (c.classe) p.setAttribute('class', c.classe);
        else p.setAttribute('fill', c.cor);
        p.setAttribute('stroke', 'var(--borda)');
        p.setAttribute('stroke-width', '0.6');
        if (c.codarea !== null) p.setAttribute('data-codarea', c.codarea);
        /* O clique é ligado por <path>, não por delegação no <svg>: são 5570
         * elementos e a delegação exigiria um `closest` por clique de qualquer
         * coisa. O nome do município NÃO mora aqui — a malha do IBGE só traz
         * `codarea` em `properties` — e por isso quem sabe o nome é quem
         * chamou, via `aoClicar(codarea)`. */
        if (opcoes.aoClicar && c.codarea !== null) {
            p.addEventListener('click', () => opcoes.aoClicar(c.codarea));
        }
        svg.appendChild(p);
    }

    if (rotulos) {
        for (const c of g.caminhos) {
            if (!c.centroide) continue;
            const [x, y] = projectaPonto(c.centroide, caixaDaGeo(geojson));
            const t = doc.createElementNS(NS, 'text');
            t.setAttribute('x', (x * LARGURA).toFixed(1));
            t.setAttribute('y', (y * ALTURA).toFixed(1));
            t.setAttribute('class', CLASSE_ROTULO);
            t.setAttribute('text-anchor', 'middle');
            t.textContent = c.codarea === null ? '' : rotuloDeEstado(c.codarea);
            svg.appendChild(t);
        }
    }

    svg.appendChild(construirLegenda(doc, g.legenda, opcoes));
    el.appendChild(svg);

    return { svg, geometria: g, vazio: false };
}

/** A legenda em SVG, na base do mapa. */
function construirLegenda(doc, legenda, opcoes = {}) {
    const NS = 'http://www.w3.org/2000/svg';
    const g = doc.createElementNS(NS, 'g');
    g.setAttribute('class', 'mapa-legenda');

    const y = ALTURA - 14;
    let x = 8;
    for (const b of legenda) {
        if (b.titulo) continue;
        const r = doc.createElementNS(NS, 'rect');
        r.setAttribute('x', String(x));
        r.setAttribute('y', String(y - 9));
        r.setAttribute('width', '14');
        r.setAttribute('height', '10');
        if (b.classe) r.setAttribute('class', CLASSE_SEM_DADO);
        else r.setAttribute('fill', b.cor);
        r.setAttribute('stroke', 'var(--borda)');
        r.setAttribute('stroke-width', '0.4');
        g.appendChild(r);

        const t = doc.createElementNS(NS, 'text');
        t.setAttribute('x', String(x + 18));
        t.setAttribute('y', String(y));
        t.setAttribute('class', CLASSE_ROTULO);
        t.textContent = b.rotulo;
        g.appendChild(t);

        x += 18 + String(b.rotulo).length * 6 + 14;
        if (x > LARGURA - 120) break;
    }
    return g;
}

/**
 * Rótulo curto de uma UF a partir do codarea.
 *
 * O codarea de estado é o próprio código da UF ("11"), e o nome vem do modelo
 * do servidor, não da malha — a malha do IBGE não traz nome. Sem nome, o
 * rótulo é o próprio código, que é feio mas não é mentira.
 */
function rotuloDeEstado(codarea) {
    return String(codarea);
}