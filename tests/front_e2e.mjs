/* tests/front_e2e.mjs — o front contra o servidor de verdade.
 *
 * tests/test_web.mjs mede o comportamento do front com um fetch falso: é o
 * que prova que NADA é buscado antes do clique. Isso não prova que o que sai
 * do servidor real é consumido corretamente — os dois lados podem estar
 * errados em direções opostas e o teste com fetch falso continua verde.
 *
 * Este arquivo é a outra metade: `criarApp` com o fetch global (o mesmo
 * objeto que o navegador entrega), contra um servidor no endereço que se
 * passa na linha de comando. A checagem é o que a pessoa veria na tela
 * depois de clicar em "Filtrar": os quatro cartões de KPI com número, a
 * tabela com as linhas que a API devolveu, e a geometria com os 5570
 * polígonos do IBGE.
 *
 * Não é navegador: não há canvas nem layout. É o código do front, o mesmo,
 * conversando com o servidor, o mesmo. O que prova é o CONTRATO — que o
 * formato da spec §8 é o que o front lê, e que nenhum 0 substitui um
 * ausente. O que não prova é que a tela está bonita.
 *
 * Não entra no `make test`: precisa de um servidor no ar, e uma suíte que
 * depende de rede é uma suíte que falha na máquina errada.
 *
 * Uso: node tests/front_e2e.mjs [http://127.0.0.1:8080]
 */
import { strict as assert } from 'node:assert';
import http from 'node:http';
import zlib from 'node:zlib';

import { criarApp } from '../web/app.js';
import { paramsDaConsulta, textoOpcoes } from '../web/filters.js';
import { docDeTeste } from './fake_dom.mjs';

const BASE = process.argv[2] || 'http://127.0.0.1:8080';

let passos = 0;
let falhas = 0;

async function passo(nome, fn) {
    passos++;
    try {
        await fn();
        console.log(`ok    ${nome}`);
    } catch (e) {
        falhas++;
        console.log(`FALHA ${nome}\n      ${e && e.message ? e.message : e}`);
    }
}

/**
 * Lê um corpo gzip pelo `http` do node e devolve os bytes crus, os
 * descomprimidos e o Content-Length declarado.
 *
 * Por que não pelo `fetch`: o undici (node v24.21.0) morre com
 * `assert(!this.paused)` ao descompactar um corpo gzip de ~1,4 MB. Não é
 * defeito do servidor — reproduzido com um servidor Python de quinze linhas
 * que manda exatamente o mesmo cabeçalho e o mesmo tamanho, e que também
 * mata o processo. `curl` e o navegador leem os mesmos bytes sem esforço.
 * O par `http` + `zlib` é o mesmo que o undici usa por dentro, então o que
 * o passo mede — o conteúdo — continua sendo o conteúdo que o servidor
 * escreveu.
 */
function brutoGz(url) {
    return new Promise((resolve, reject) => {
        const u = new URL(url);
        http.get({
            hostname: u.hostname,
            port: u.port,
            path: u.pathname + u.search,
            headers: { 'Accept-Encoding': 'gzip' },
        }, (res) => {
            const partes = [];
            res.on('data', (c) => partes.push(c));
            res.on('error', reject);
            res.on('end', () => {
                const raw = Buffer.concat(partes);
                const gz = /\bgzip\b/.test(res.headers['content-encoding'] || '');
                resolve({
                    raw: raw.length,
                    declared: Number(res.headers['content-length']),
                    encoding: gz ? 'gzip' : 'identity',
                    plano: (gz ? zlib.gunzipSync(raw) : raw).toString('utf8'),
                });
            });
        }).on('error', reject);
    });
}

/* No navegador, `fetch('/api/consulta?...')` resolve contra a origem da
 * página. No node não há `location`, e o fetch global rejeita caminho
 * relativo — o que é a resposta certa dele, não um defeito do front. O
 * prefixo abaixo é o que a barra de endereço faz pelo front. */
function fetchDoNavegador(caminho, init) {
    return globalThis.fetch(String(caminho).startsWith('http')
        ? caminho : BASE + caminho, init);
}

const doc = docDeTeste();
const app = criarApp({ doc, fetch: fetchDoNavegador, logger: null });
const estado = () => app.estado();

/* ------------------------------------------------------------------ */

/* 1. Nada é buscado antes do clique. Se esta asserção falhar, o problema é o
 *    mais grave possível: a tela cheia de dado que ninguém pediu, que é o
 *    que a spec §10 proíbe. */
await passo('abrir não busca nada (spec §10)', async () => {
    const e = estado();
    assert.equal(e.consultado, false, 'o estado começa não consultado');
    assert.equal(e.registros.length, 0, 'sem registros antes do clique');
    const valores = doc.porId.kpis.querySelectorAll('.kpi')
        .map((c) => c.querySelector('.kpi-valor').textContent.trim());
    assert.deepEqual(valores, ['--', '--', '--', '--'],
        `os KPIs têm o placeholder do HTML; veio ${JSON.stringify(valores)}`);
    assert.equal(doc.porId.uf.children.length, 0,
        '#uf nasce vazio: as opções vêm do servidor, depois do clique');
});

/* 2. As opções vêm do servidor real e são 27 UFs de verdade (C6). */
await passo('/api/opcoes lista anos e UFs do servidor', async () => {
    await app.filtrar();
    assert.equal(estado().opcoesCarregadas, true, 'as opções foram carregadas');

    /* O que interessa é o que ficou NA TELHA, não o objeto interno: o
     * seletor de UF tem de listar as 27 do IBGE (C6), e o de ano não pode
     * oferecer as lacunas da série — nem 2007 nem 2022, que o SIDRA não
     * publica. */
    const ufs = doc.porId.uf.children.map((c) => c.textContent.trim());
    assert.ok(ufs.length >= 28,
        `27 UFs mais "Todas as UFs"; veio ${ufs.length}`);
    assert.ok(ufs.some((u) => u === 'AC'), `Acre está na lista; veio ${JSON.stringify(ufs)}`);
    assert.ok(ufs.some((u) => u === 'SP'), 'São Paulo está na lista');
    assert.ok(ufs.some((u) => u === 'Todas as UFs'), 'e a opção de não filtrar');

    const anos = doc.porId.ano.children.map((c) => c.textContent.trim());
    assert.ok(anos.length >= 22, `22 anos publicados; veio ${anos.length}`);
    assert.ok(anos.includes('2024'), `2024 está no seletor; veio ${JSON.stringify(anos)}`);
    assert.ok(!anos.includes('2007'), '2007 é lacuna e não pode ser oferecido');
    assert.ok(!anos.includes('2022'), '2022 é lacuna e não pode ser oferecido');
});

/* 3. O clique preenche os KPIs com número real. Este é o "o front mostra
 *    dado" da tarefa: os quatro cartões saem de "--" e nenhum é
 *    placeholder. */
await passo('os quatro KPIs mostram número depois de Filtrar', async () => {
    const valores = doc.porId.kpis.querySelectorAll('.kpi')
        .map((c) => c.querySelector('.kpi-valor').textContent.trim());
    assert.ok(!valores.some((v) => v === '--'),
        `os KPIs não podem continuar em "--"; veio ${JSON.stringify(valores)}`);
    assert.ok(valores.every((v) => /\d/.test(v)),
        `todos os KPIs têm dígito; veio ${JSON.stringify(valores)}`);

    const e = estado();
    assert.equal(e.consultado, true, 'o estado é "consultado"');
    assert.equal(typeof e.total, 'number', 'total é número');
    assert.ok(e.total > 5000,
        `o total é o do conjunto, não o da página; veio ${e.total}`);
    assert.ok(e.kpis && typeof e.kpis.max === 'number', 'max é número');
    assert.ok(e.kpis.com_dado > 0, 'há municípios com dado');
    assert.ok(e.kpis.min > 0, `o mínimo não é 0 nem negativo; veio ${e.kpis.min}`);
});

/* 4. A tabela tem as linhas que a API devolveu. É o `registros` da spec §8:
 *    se o app ainda lesse `dados`, este passo seria o que falharia. */
await passo('a tabela é montada com os registros da resposta', async () => {
    const e = estado();
    assert.ok(e.registros.length > 0, 'há registros no estado');
    assert.ok(doc.porId.tabela.children.length > 0, 'a tabela tem linhas no DOM');
    /* O nome com acento tem que chegar ao texto, e não como escape bruto.
     * A comparação é por texto, não por JSON.stringify: o elemento guarda
     * `ownerDocument`, e serializar a árvore é um erro de circularidade que
     * não tem nada a ver com o que se quer medir. */
    const texto = doc.porId.tabela.textContent;
    assert.ok(texto.includes('São Paulo'),
        `o primeiro nome da tabela é São Paulo, com acento; veio "${texto.slice(0, 80)}"`);
    assert.ok(!texto.includes('\\u00'),
        'o texto não chega escapado em \\u');
});

/* 5. A busca por nome com acento acha o município (spec §9: o nome é
 *    idêntico no mapa, na tabela e na busca). */
await passo('busca por município com acento acha o município', async () => {
    const q = new URLSearchParams(paramsDaConsulta({
        fonte: 'ibge', indicador: 'populacao', nivel: 'municipio',
        uf: '', ano: '2024', busca: 'São Paulo', ordenar: 'valor_desc',
        limite: '',
    })).toString();
    assert.ok(q.includes('busca=S'), `a busca vai percent-encodada; veio ${q}`);
    assert.ok(!/busca=[^&]*\s/.test(q),
        `o espaço da busca tem de virar %20; veio ${q}`);

    const r = await fetchDoNavegador(`/api/consulta?${q}`).then((x) => x.json());
    assert.ok(r.total >= 1, `a busca por "São Paulo" acha algo; veio ${r.total}`);
    assert.equal(r.registros[0].nome, 'São Paulo', 'o primeiro é São Paulo');
    assert.ok(r.registros[0].valor > 10000000,
        `São Paulo tem 11,9 milhões em 2024; veio ${r.registros[0].valor}`);
});

/* 6. Ausente é null, nunca 0 (spec §6). */
await passo('lacuna da série é recusada, e ausente vira "--" e não 0', async () => {
    const res = await fetchDoNavegador('/api/consulta?fonte=ibge&ano=2007');
    const corpo = await res.json().catch(() => null);
    assert.equal(res.status, 400, 'ano de lacuna é 400');
    assert.ok(corpo && corpo.erro && typeof corpo.erro.mensagem === 'string',
        'a recusa tem mensagem, não lista vazia');
    assert.ok(corpo.erro.mensagem.includes('2007'),
        `a mensagem nomeia o ano; veio "${corpo.erro.mensagem}"`);
    assert.equal(textoOpcoes(-1), '--', 'valor ausente vira "--"');
    assert.notEqual(textoOpcoes(-1), '0', 'valor ausente nunca vira 0');
    assert.equal(textoOpcoes(null), '--', 'null vira "--"');
});

/* 7. Erro do servidor aparece na tela com a mensagem DELE, não genérica. */
await passo('erro do servidor vai para #kpis com a mensagem do servidor', async () => {
    const doc3 = docDeTeste();
    doc3.porId.ano.value = '1899';
    const app3 = criarApp({ doc: doc3, fetch: fetchDoNavegador, logger: null });
    await app3.filtrar();
    /* A mensagem é a DO SERVIDOR, palavra por palavra — não um texto
     * genérico de "deu errado". E o ano que a pessoa digitou aparece nela,
     * que é o que permite saber qual filtro tirar. */
    assert.equal(doc3.porId.kpis.textContent, 'ano "1899" inválido',
        `#kpis mostra a mensagem do servidor; veio "${doc3.porId.kpis.textContent}"`);
});

/* 8. O mapa vem com geometria de verdade: as 5570 feições do IBGE, com o
 *    `codarea` que é a chave de junção da spec §8. */
await passo('/api/geo/municipios devolve 5570 polígonos', async () => {
    const bruto = await brutoGz(`${BASE}/api/geo/municipios`);
    assert.equal(bruto.encoding, 'gzip', 'a resposta vem comprimida');
    assert.equal(bruto.raw, bruto.declared,
        `Content-Length bate com os bytes: ${bruto.raw} vs ${bruto.declared}`);
    assert.equal(bruto.plano.length, 1485243,
        `1.485.243 bytes de geometria; veio ${bruto.plano.length}`);
    const r = JSON.parse(bruto.plano);
    assert.equal(r.type, 'FeatureCollection', 'é uma FeatureCollection');
    assert.equal(r.features.length, 5570,
        `5570 municípios; veio ${r.features.length}`);
    assert.ok(r.features[0].properties && r.features[0].properties.codarea,
        'cada feição tem codarea, que é a chave de junção da spec');
});

/* ------------------------------------------------------------------ */

console.log();
console.log(`${BASE}: ${passos - falhas} de ${passos} passos passaram`);
if (falhas) process.exit(1);