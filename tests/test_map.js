/* tests/test_map.js — o coroplético, em node, sem jsdom.
 *
 * A Tarefa 11 do plano pede quatro asserções, e elas estão aqui na letra:
 *
 *   1. FeatureCollection com 2 polígonos e valores {a:1,b:2} → 2 <path> com
 *      `fill` distintos.
 *   2. Código sem valor → preenchimento pelo token de AUSÊNCIA do tema
 *      (classe `mapa-sem-dado`, que o styles.css resolve para
 *      `var(--ausencia)`), e NÃO a cor mais baixa da escala. A distinção
 *      importa: se "sem dado" dividisse a escala junto com os valores, o mapa
 *      ficaria todo pintado e a cor do mais baixo pareceria valor baixo em vez
 *      de ausência — que é a mentira que a spec §10 proíbe ("mapa em cinza
 *      neutro" ao abrir).
 *
 *      Por que classe e não atributo `fill`: o literal que o plano pedia era
 *      #e5e7eb, claro, e a direção visual da Tarefa 10 saiu escura
 *      (--fundo #0e1116, cartão #161b22) com variante clara por
 *      prefers-color-scheme. #e5e7eb nesse fundo é quase branco, e o município
 *      SEM DADO viraria a categoria mais brilhante do mapa. O token
 *      `--ausencia` já existe no styles.css para os dois temas, e atributo SVG
 *      não resolve `var()` — só CSS resolve. Ver o ruling no plano.
 *   3. Todos os valores iguais → a escala não divide por zero.
 *   4. Nenhum valor → todo o mapa em #e5e7eb.
 *
 * Por que o arquivo é .js e não .mjs: o front é servido como estático e não
 * tem build step, então o sufixo do módulo é escolha de quem importa. Os testes
 * usam a mesma convenção do que importam; `node` resolve `./map.js` pelo
 * package-less ESM quando o arquivo é .js com sintaxe de módulo e o diretório
 * tem package.json com "type": "module" — que este projeto não tem. Por isso
 * o import leva o caminho relativo com extensão, e o Makefile roda o arquivo
 * com o mesmo interpretador dos demais testes do front.
 *
 * Execução: `make test` (o alvo `test` roda este arquivo) ou
 *           `node tests/test_map.js`
 */
import { strict as assert } from 'node:assert';

import {
    CLASSE_SEM_DADO,
    bandasDaEscala,
    corDoCodigo,
    caixaDaGeo,
    geometriaMapa,
    desenharMapa,
    caminhoDoAnel,
    projectaPonto,
} from '../web/map.js';
import { criaEl } from './fake_dom.mjs';

let testes = 0;
let falhas = 0;
const falhasDetalhadas = [];

function teste(nome, fn) {
    testes++;
    try {
        fn();
    } catch (e) {
        falhas++;
        falhasDetalhadas.push(`${nome}: ${e && e.message ? e.message : e}`);
    }
}

/* ------------------------------------------------------------------ */
/* fixtures                                                            */
/* ------------------------------------------------------------------ */

/**
 * FeatureCollection de dois polígonos quadrados, em graus×1000 (a unidade que
 * src/geo.c emite: 1 unidade = ~111 m, sempre inteira).
 *
 * O primeiro quadrado é o de "1100015" (Rondônia, o menor código) e o segundo
 * é "4300002" — separados no eixo para que a projeção de cada um possa ser
 * conferida isoladamente.
 */
function geoDoisMunicipios() {
    return {
        type: 'FeatureCollection',
        features: [
            {
                type: 'Feature',
                properties: { codarea: '1100015', centroide: [-62075, -12500] },
                geometry: {
                    type: 'Polygon',
                    coordinates: [[[-63000, -13000], [-61000, -13000],
                        [-61000, -12000], [-63000, -12000], [-63000, -13000]]],
                },
            },
            {
                type: 'Feature',
                properties: { codarea: '4300002', centroide: [-51500, -32000] },
                geometry: {
                    type: 'Polygon',
                    coordinates: [[[-52000, -33000], [-51000, -33000],
                        [-51000, -31000], [-52000, -31000], [-52000, -33000]]],
                },
            },
        ],
    };
}

/** Dois quadrados cujos valores são 1 e 2 — a escala tem duas faixas distintas. */
const VALORES_UM_E_DOIS = { 1100015: 1, 4300002: 2 };

/**
 * Percorre a árvore do DOM falso e devolve todos os elementos com a tag pedida.
 *
 * O `querySelectorAll` de tests/fake_dom.mjs casa por CLASSE, e o que o
 * coroplético precisa verificar é a tag e o atributo `fill` de cada <path>.
 * Um helper que só visse por classe passaria com o mapa vazio — que é
 * exatamente a asserção-ceba que aprovou `ufs: []` na Tarefa 7.
 */
function tagsDe(raiz, tag) {
    const alvo = String(tag).toUpperCase();
    const out = [];
    const visita = (no) => {
        if (!no) return;
        if (no.tagName === alvo) out.push(no);
        for (const f of no.children || []) visita(f);
    };
    visita(raiz);
    return out;
}

/** Documento falso e o elemento que recebe o mapa. */
function alvoDeTeste() {
    const doc = { createElement: (t) => criaEl(t, doc), createElementNS: (ns, t) => criaEl(t, doc) };
    return { doc, el: criaEl('div', doc) };
}

/* ------------------------------------------------------------------ */
/* asserção 1: dois polígonos, dois fills                               */
/* ------------------------------------------------------------------ */

teste('dois municípios com valores diferentes saem com fill distinto', () => {
    const geo = geoDoisMunicipios();
    const g = geometriaMapa(geo, VALORES_UM_E_DOIS, {});

    assert.equal(g.caminhos.length, 2, 'a geometria tem um caminho por município');
    const fills = g.caminhos.map((c) => c.cor);
    assert.equal(fills.length, 2, 'são dois fills');
    assert.notEqual(fills[0], fills[1],
        `os dois municípios precisam de cores diferentes; vieram ${fills[0]} e ${fills[1]}`);
});

teste('cada <path> desenhado carrega o fill da sua faixa', () => {
    const { doc, el } = alvoDeTeste();
    desenharMapa(el, geoDoisMunicipios(), VALORES_UM_E_DOIS, { doc });

    const paths = tagsDe(el, 'path');
    assert.equal(paths.length, 2, `o SVG tem 2 <path>; vieram ${paths.length}`);
    const fills = paths.map((p) => p.getAttribute('fill'));
    assert.equal(fills.filter(Boolean).length, 2, 'todo <path> tem fill — nenhum sem atributo');
    assert.notEqual(fills[0], fills[1], `fills distintos; vieram ${fills[0]} e ${fills[1]}`);
});

/* ------------------------------------------------------------------ */
/* asserção 2: código sem valor é cinza neutro, não a cor mais baixa    */
/* ------------------------------------------------------------------ */

teste('código sem valor fica na cor de ausência, e não na mais baixa da escala', () => {
    // Só um dos dois tem dado. É esse o caso que a escala não pode absorver:
    // com um valor presente, existe uma cor "mais baixa" real, e é para ela
    // que o município ausente NÃO pode ser pintado.
    const valores = { 1100015: 5 };
    const g = geometriaMapa(geoDoisMunicipios(), valores, {});

    const semDado = g.caminhos.find((c) => c.codarea === '4300002');
    const comDado = g.caminhos.find((c) => c.codarea === '1100015');

    assert.equal(semDado.classe, CLASSE_SEM_DADO,
        `sem dado tem que levar a classe de ausência; veio "${semDado.classe}"`);
    assert.notEqual(comDado.classe, CLASSE_SEM_DADO,
        'o que tem dado não pode receber a classe de ausência');
    assert.ok(comDado.cor, 'o que tem dado recebe uma cor concreta da escala');
});

teste('o <path> sem dado não recebe atributo fill nenhum', () => {
    // Mesmo caso: 4300002 não tem valor, 1100015 tem.
    // A classe é quem resolve a cor. Um `fill` residual sobrescreveria a
    // classe no CSS e o mapa voltaria a mentir: sem dado viraria branco no
    // tema escuro. A asserção é sobre a ausência do atributo, não sobre o
    // valor dele.
    const { doc, el } = alvoDeTeste();
    desenharMapa(el, geoDoisMunicipios(), { 1100015: 5 }, { doc });

    const paths = tagsDe(el, 'path');
    const semDado = paths.find((p) => (p.getAttribute('class') || '').includes(CLASSE_SEM_DADO));
    assert.ok(semDado, 'existe <path> com a classe de ausência');
    assert.equal(semDado.getAttribute('fill'), null,
        `o <path> de ausência não leva fill; veio "${semDado.getAttribute('fill')}"`);
});

teste('nenhuma faixa da escala usa a classe de ausência', () => {
    // Se a escala descesse até o cinza, "sem dado" e "o menor valor" ficariam
    // idênticos na tela e o usuário não distinguiria os dois casos.
    const bandas = bandasDaEscala([5, 900, 40, 7000, 300000], 5);
    assert.ok(bandas.length >= 2, `a escala tem várias faixas; vieram ${bandas.length}`);
    for (const b of bandas) {
        assert.notEqual(b.classe, CLASSE_SEM_DADO,
            `nenhuma faixa da escala pode ser a classe de ausência; veio ${b.classe}`);
        assert.match(b.cor, /^#[0-9a-f]{6}$/i,
            `a faixa tem cor concreta própria; veio "${b.cor}"`);
    }
});

/* ------------------------------------------------------------------ */
/* asserção 3: valores iguais não dividem por zero                     */
/* ------------------------------------------------------------------ */

teste('todos os valores iguais não geram NaN nem Infinity', () => {
    const geo = geoDoisMunicipios();
    const g = geometriaMapa(geo, { 1100015: 7, 4300002: 7 }, {});

    for (const c of g.caminhos) {
        assert.ok(!/NaN|Infinity|undefined/.test(c.d),
            `o caminho é numérico; veio "${c.d}"`);
        assert.match(c.cor, /^#[0-9a-f]{6}$/i, `a cor é hex; veio "${c.cor}"`);
    }
    assert.ok(g.viewBox && !/NaN|Infinity/.test(g.viewBox),
        `o viewBox é numérico; veio "${g.viewBox}"`);
});

teste('escala com um único valor não divide por zero', () => {
    const bandas = bandasDaEscala([42], 5);
    for (const b of bandas) {
        assert.ok(!Number.isNaN(b.min) && !Number.isNaN(b.max),
            `a faixa ${b.cor} tem limites numéricos`);
        assert.match(b.cor, /^#[0-9a-f]{6}$/i, `a faixa ${b.cor} é uma cor válida`);
    }
});

teste('escala com lista vazia não devolve faixa nenhuma em vez de NaN', () => {
    assert.deepEqual(bandasDaEscala([], 5), [],
        'sem valor não há faixa: o mapa inteiro é o cinza de ausência');
});

/* ------------------------------------------------------------------ */
/* asserção 4: nenhum valor → todo o mapa em #e5e7eb                   */
/* ------------------------------------------------------------------ */

teste('nenhum valor pinta o mapa inteiro de #e5e7eb', () => {
    const geo = geoDoisMunicipios();
    for (const valores of [{}, null, undefined]) {
        const g = geometriaMapa(geo, valores, {});
        assert.equal(g.caminhos.length, 2, 'os dois caminhos continuam existindo');
        for (const c of g.caminhos) {
            assert.equal(c.classe, CLASSE_SEM_DADO,
                `com valores=${JSON.stringify(valores)} o mapa é todo ausente; veio "${c.classe}"`);
        }
    }
});

teste('o desenho sem valor some põe cinza em todos os <path>', () => {
    const { doc, el } = alvoDeTeste();
    desenharMapa(el, geoDoisMunicipios(), {}, { doc });
    const paths = tagsDe(el, 'path');
    assert.equal(paths.length, 2, 'o desenho do mapa não some quando não há valor');
    for (const p of paths) {
        assert.equal(p.getAttribute('class'), CLASSE_SEM_DADO,
            'todo <path> marcado como ausência');
        assert.equal(p.getAttribute('fill'), null,
            'e nenhum deles com fill próprio');
    }
});

/* ------------------------------------------------------------------ */
/* geometria: projeção, viewBox e caminho                                */
/* ------------------------------------------------------------------ */

teste('a projeção usa viewBox e não escreve coordenada absoluta no atributo', () => {
    // As coordenadas chegam em graus×1000 e podem chegar negativas (lon
    // oeste). Escrever o número cru no `d` punha o desenho para fora do
    // viewBox; quem projeta é a função, e o `d` tem que estar deslocado para
    // a origem do viewBox.
    const geo = geoDoisMunicipios();
    const g = geometriaMapa(geo, VALORES_UM_E_DOIS, {});
    for (const c of g.caminhos) {
        const nums = c.d.match(/-?\d+(\.\d+)?/g) || [];
        assert.ok(nums.length > 0, 'o caminho tem coordenadas');
        for (const n of nums) {
            const v = Number(n);
            assert.ok(Number.isFinite(v), `coordenada numérica; veio "${n}"`);
            assert.ok(v >= 0, `o atributo d não tem coordenada negativa; veio "${n}"`);
        }
    }
});

teste('a caixa da geometria contém todos os vértices projetados', () => {
    const geo = geoDoisMunicipios();
    const [x0, y0, x1, y1] = caixaDaGeo(geo);
    assert.ok(x0 < x1 && y0 < y1, `a caixa tem área; veio [${x0},${y0},${x1},${y1}]`);
    for (const f of geo.features) {
        for (const anel of f.geometry.coordinates) {
            for (const [x, y] of anel) {
                assert.ok(x >= x0 && x <= x1, `x ${x} dentro de [${x0},${x1}]`);
                assert.ok(y >= y0 && y <= y1, `y ${y} dentro de [${y0},${y1}]`);
            }
        }
    }
});

teste('a projeção leva a caixa para [0,1] e inverte o eixo y', () => {
    // O GeoJSON traz [x=longitude, y=latitude] em graus×1000, e a longitude
    // oeste do Brasil é NEGATIVA. Sem deslocar pela caixa, o `d` sairia
    // negativo e o desenho cairia fora do viewBox — que é o que a asserção
    // "o `d` não tem coordenada negativa" mede.
    const caixa = [0, 0, 100, 200];
    assert.deepEqual(projectaPonto([0, 0], caixa), [0, 1],
        'a menor longitude vai para a esquerda e a menor latitude para o BAIXO');
    assert.deepEqual(projectaPonto([100, 200], caixa), [1, 0],
        'a maior latitude sobe: no SVG o y cresce para baixo, no GeoJSON não');
    assert.deepEqual(projectaPonto([50, 100], caixa), [0.5, 0.5], 'o centro vai ao centro');
});

teste('uma geometria MultiPolygon gera um caminho só, com um sub-ponto por polígono', () => {
    // Não é hypothético: na malha real, 2773 dos 5570 municípios são
    // MultiPolygon (ilhas, arquipélagos). Um mapa que só lê Polygon deixa
    // metade do Brasil em branco — e o teste de "2 polígonos" do brief, sendo
    // Polygon, passaria em cima dessa metade.
    const multi = {
        type: 'FeatureCollection',
        features: [{
            type: 'Feature',
            properties: { codarea: '1100015', centroide: [-62075, -12500] },
            geometry: {
                type: 'MultiPolygon',
                coordinates: [
                    [[[-63000, -13000], [-62000, -13000], [-62000, -12500], [-63000, -12500], [-63000, -13000]]],
                    [[[-61000, -12000], [-60500, -12000], [-60500, -11800], [-61000, -11800], [-61000, -12000]]],
                ],
            },
        }],
    };
    const g = geometriaMapa(multi, { 1100015: 3 }, {});
    assert.equal(g.caminhos.length, 1, 'um município é um caminho só, mesmo com duas ilhas');
    const d = g.caminhos[0].d;
    assert.ok(!/NaN|Infinity|undefined/.test(d), `o caminho é numérico; veio "${d}"`);
    // Dois polígonos, cada um fechado com Z: dois sub-caminhos.
    const sub = d.split('M').length - 1;
    assert.equal(sub, 2, `os dois polígonos viram dois sub-caminhos; vieram ${sub}`);
});

teste('caminhoDoAnel fecha o anel com Z e não deixa coordenada nula', () => {
    const anel = [[0, 0], [10, 0], [10, 10], [0, 10]];
    const d = caminhoDoAnel(anel);
    assert.ok(/^M/.test(d), `começa com moveto; veio "${d}"`);
    assert.ok(/Z$/.test(d), `fecha com Z; veio "${d}"`);
    assert.ok(!/NaN|Infinity|undefined/.test(d), `sem NaN; veio "${d}"`);
});

teste('caminhoDoAnel com anel vazio devolve string vazia, não "MNaNNaNNaNZ"', () => {
    assert.equal(caminhoDoAnel([]), '', 'anel vazio não gera path quebrado');
    assert.equal(caminhoDoAnel(null), '', 'anel nulo idem');
});

teste('anel com vértice ilegível não vaza NaN no caminho', () => {
    const d = caminhoDoAnel([[0, 0], [Number.NaN, 5], [10, 10]]);
    assert.ok(!/NaN/.test(d), `um vértice ruim não pode contaminar o d; veio "${d}"`);
});

teste('anel de um vértice só não gera moveto vazio', () => {
    const d = caminhoDoAnel([[5, 5]]);
    assert.ok(!/NaN/.test(d), `sem NaN; veio "${d}"`);
    assert.ok(/M/.test(d), `desenha o ponto; veio "${d}"`);
});

/* ------------------------------------------------------------------ */
/* desenho: legenda, rótulo e ausência de lixo no `d`                  */
/* ------------------------------------------------------------------ */

teste('a legenda traz as faixas e a faixa "sem dado"', () => {
    const g = geometriaMapa(geoDoisMunicipios(), VALORES_UM_E_DOIS, {});
    assert.ok(Array.isArray(g.legenda), 'a legenda é uma lista');
    assert.ok(g.legenda.length >= 2, `a legenda lista as faixas; vieram ${g.legenda.length}`);
    const semDado = g.legenda.find((b) => b.semDado);
    assert.ok(semDado, 'a legenda diz o que é ausência');
    assert.equal(semDado.classe, CLASSE_SEM_DADO,
        'a faixa de ausência usa o mesmo token do mapa — legenda e mapa não podem divergir');
    assert.equal(semDado.cor, null,
        'a faixa de ausência não leva cor própria: quem pinta é a classe');
});

teste('o desenho produz SVG com viewBox e sem NaN em atributo nenhum', () => {
    const { doc, el } = alvoDeTeste();
    desenharMapa(el, geoDoisMunicipios(), VALORES_UM_E_DOIS, { doc });

    const svgs = tagsDe(el, 'svg');
    assert.equal(svgs.length, 1, `um <svg>; vieram ${svgs.length}`);
    const vb = svgs[0].getAttribute('viewBox');
    assert.ok(vb && !/NaN|Infinity/.test(vb), `viewBox numérico; veio "${vb}"`);

    const visita = (no) => {
        for (const [k, v] of Object.entries(no.atributos || {})) {
            assert.ok(!/NaN|Infinity|undefined/.test(String(v)),
                `atributo ${k} sem NaN; veio "${v}"`);
        }
        for (const f of no.children || []) visita(f);
    };
    visita(el);
});

teste('o desenho aceita o doc por parâmetro e não toca no global', () => {
    // Se map.js pegasse `document` do escopo de módulo, o import em node
    // quebraria e a suíte inteira do front cairia junto.
    const { doc, el } = alvoDeTeste();
    desenharMapa(el, geoDoisMunicipios(), {}, { doc });
    assert.ok(tagsDe(el, 'svg').length === 1, 'o desenho aconteceu com o doc injetado');
});

/* ------------------------------------------------------------------ */
/* relatório                                                           */
/* ------------------------------------------------------------------ */

if (falhas > 0) {
    for (const f of falhasDetalhadas) console.error(`FALHA ${f}`);
    console.error(`${falhas} de ${testes} testes do mapa falharam`);
    process.exit(1);
}
console.log(`OK: ${testes} testes do mapa passaram`);