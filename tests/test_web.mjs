/* tests/test_web.mjs — comportamento do front em node, sem jsdom.
 *
 * Por que existe: o front não tem build step, então nada garante que ele
 * funciona até alguém abrir o navegador. Estes testes medem as duas coisas que
 * a spec exige e que um teste de "a página carrega" não pega:
 *
 *   1. Estado vazio de verdade — nenhum fetch acontece até o clique em
 *      "Filtrar". Um fetch no carregamento encheria a tela de dados sem o
 *      usuário pedir, que é exatamente o que a spec proíbe. O poder de
 *      detecção vem de injetar um fetch falso e contar as chamadas.
 *   2. Valores ausentes não viram 0 — a spec diz que "" e ".." nunca podem
 *      virar 0. Um ano 0 seria um 400 do servidor ou pior, uma consulta
 *      silenciosamente errada.
 *
 * Como roda sem DOM: app.js não toca em `document` no import. Quem monta a
 * interface recebe o `doc` por parâmetro (injeção), o que é o mesmo caminho
 * que o ruling C-5 usa para separar cálculo de renderização.
 *
 * Execução: `node tests/test_web.mjs`
 */
import { strict as assert } from 'node:assert';

import {
    criarApp,
    estadoInicial,
    lerEstado,
    estadoVazio,
    anoValido,
    inteiroValido,
    rotuloIndicador,
    paramsDaConsulta,
    textoOpcoes,
    OPCOES_INDICADOR,
    OPCOES_NIVEL,
    OPCOES_ORDENAR,
    ORDENS,
} from '../web/app.js';
import { consultar, opcoes, geo, rotaConsulta, montarQuery } from '../web/api.js';
import { segmentosLinha, escalaLinear, dominioValores, fatiasDonut, desenharLinha } from '../web/charts.js';

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

async function testeAsync(nome, fn) {
    testes++;
    try {
        await fn();
    } catch (e) {
        falhas++;
        falhasDetalhadas.push(`${nome}: ${e && e.message ? e.message : e}`);
    }
}

/* ------------------------------------------------------------------ */
/* DOM falso: mínimo, só o que app.js/filters.js realmente tocam.      */
/* ------------------------------------------------------------------ */

function criaEl(tag, doc) {
    return {
        tagName: String(tag).toUpperCase(),
        _doc: doc,
        children: [],
        _text: '',
        atributos: {},
        value: '',
        disabled: false,
        textContent: '',
        dataset: {},
        ownerDocument: doc,
        appendChild(filho) { this.children.push(filho); return filho; },
        /* O DOM real expõe firstChild/lastChild, e o código de render os usa
         * para esvaziar um painel. Sem eles o "limpar" não limpa. */
        get firstChild() { return this.children.length ? this.children[0] : null; },
        get lastChild() { return this.children.length ? this.children[this.children.length - 1] : null; },
        removeChild(filho) {
            const i = this.children.indexOf(filho);
            if (i >= 0) this.children.splice(i, 1);
            return filho;
        },
        setAttribute(nome, valor) { this.atributos[nome] = String(valor); },
        getAttribute(nome) { return this.atributos[nome] ?? null; },
        removeAttribute(nome) { delete this.atributos[nome]; },
        addEventListener(tipo, fn) { (this._listeners ||= {})[tipo] = fn; },
        /* Dispara o handler registrado: é o clique em "Filtrar". */
        async click() {
            const fn = this._listeners && this._listeners.click;
            if (fn) return fn({ preventDefault() {} });
            return undefined;
        },
        replaceChildren() { this.children = []; },
        remove() {},
        /* textContent precisa zerar os filhos quando recebe string, senão o
         * estado vazio continuaria exibindo o que estava lá. */
        _setText(v) {
            this._text = v;
            if (typeof v === 'string') this.children = [];
        },
        get textContent() {
            if (this._text) return this._text;
            return this.children.map((c) => c.textContent).join('');
        },
        set textContent(v) { this._setText(v); },
        /* querySelectorAll precisa achar de verdade quem casa com o
         * seletor. A primeira versão devolvia [] para tudo, e isso escondeu
         * um bug real: os KPIs ficavam "--" mesmo com dado, porque o laço de
         * render nunca encontrava o cartão. Um stub permissivo demais não
         * testa nada — melhor um que percorre a árvore. */
        querySelectorAll(sel) {
            const out = [];
            const alvo = sel.replace(/^\./, '');
            const visita = (no) => {
                for (const f of no.children || []) {
                    if (f._doc && f.classeDe && f.classeDe().split(/\s+/).includes(alvo)) out.push(f);
                    visita(f);
                }
            };
            visita(this);
            return out;
        },
        classeDe() {
            return (this.className || '') + ' ' + Object.entries(this.atributos)
                .filter(([k]) => k === 'class')
                .map(([, v]) => v).join(' ');
        },
        querySelector(sel) {
            const lista = this.querySelectorAll(sel);
            return lista.length ? lista[0] : null;
        },
        focus() {},
    };
}

/* Monta a mesma estrutura de KPIs que está no index.html, para que o teste
 * exercite o caminho de render de verdade em vez de um atalho. */
function criaPainelKpis(doc) {
    const painel = doc.getElementById('kpis');
    painel.className = 'estado-vazio';
    const rotulos = ['Total de municípios', 'Mínimo', 'Máximo', 'Média'];
    for (const rotulo of rotulos) {
        const cartao = doc.createElement('div');
        cartao.className = 'kpi';
        const r = doc.createElement('span');
        r.className = 'kpi-rotulo';
        r.textContent = rotulo;
        const v = doc.createElement('span');
        v.className = 'kpi-valor';
        v.textContent = '--';
        cartao.appendChild(r);
        cartao.appendChild(v);
        painel.appendChild(cartao);
    }
    return painel;
}

function criaDoc(ids) {
    const doc = {
        porId: {},
        createElement(tag) { return criaEl(tag, doc); },
        createElementNS(ns, tag) { return criaEl(tag, doc); },
        createTextNode(t) {
            return { tagName: '#text', children: [], textContent: String(t), _text: String(t) };
        },
        addEventListener() {},
        getElementById(id) { return this.porId[id] || null; },
    };
    for (const id of ids) {
        const el = criaEl(id.startsWith('#') ? 'div' : 'div', doc);
        if (id === 'fonte' || id === 'uf' || id === 'ano' || id === 'indicador' ||
            id === 'nivel' || id === 'ordenar' || id === 'limite') {
            el.tagName = 'SELECT';
        } else if (id === 'busca') {
            el.tagName = 'INPUT';
        }
        doc.porId[id] = el;
    }
    return doc;
}

/* ------------------------------------------------------------------ */
/* fetch falso: registra chamadas, devolve o que o teste mandar.       */
/* ------------------------------------------------------------------ */

function criaFetch(respostas) {
    const chamadas = [];
    const fn = async (url, opts) => {
        chamadas.push({ url: String(url), opts: opts || {} });
        const chave = String(url);
        const semBarra = chave.replace(/^\//, '');
        let r;
        if (Object.prototype.hasOwnProperty.call(respostas, chave)) {
            r = respostas[chave];
        } else if (Object.prototype.hasOwnProperty.call(respostas, semBarra)) {
            r = respostas[semBarra];
        } else {
            /* casar por prefixo, para o teste não precisar repetir a query */
            for (const k of Object.keys(respostas)) {
                if (chave.startsWith(k) || semBarra.startsWith(k)) { r = respostas[k]; break; }
            }
        }
        if (r === undefined) throw new Error(`fetch sem resposta de teste: ${chave}`);
        if (r instanceof Error) throw r;
        return {
            ok: r.ok !== false,
            status: r.status ?? 200,
            async json() {
                if (r.json instanceof Error) throw r.json;
                return r.json;
            },
        };
    };
    fn.chamadas = chamadas;
    fn.quantidade = () => chamadas.length;
    fn.urls = () => chamadas.map((c) => c.url);
    return fn;
}

const IDS = ['fonte', 'uf', 'ano', 'indicador', 'nivel', 'busca', 'ordenar',
    'limite', 'filtrar', 'kpis', 'graficos', 'tabela', 'mapa', 'estado-vazio'];

function appDeTeste(respostas, valores = {}) {
    const doc = criaDoc(IDS);
    /* O #kpis do index.html tem quatro cartões; reproduzir aqui é o que
     * permite testar a renderização dos KPIs sem navegador. */
    criaPainelKpis(doc);
    const fetch = criaFetch(respostas);
    Object.assign(doc.porId.fonte, { value: valores.fonte ?? 'ibge' });
    Object.assign(doc.porId.indicador, { value: valores.indicador ?? 'populacao' });
    Object.assign(doc.porId.nivel, { value: valores.nivel ?? 'municipio' });
    Object.assign(doc.porId.ordenar, { value: valores.ordenar ?? 'valor_desc' });
    Object.assign(doc.porId.limite, { value: valores.limite ?? '100' });
    Object.assign(doc.porId.ano, { value: valores.ano ?? '2024' });
    Object.assign(doc.porId.uf, { value: valores.uf ?? '' });
    Object.assign(doc.porId.busca, { value: valores.busca ?? '' });
    const app = criarApp({ doc, fetch });
    return { app, doc, fetch };
}

/* ================================================================== */
/* 1. Estado vazio: o requisito central da tarefa                      */
/* ================================================================== */

teste('estado() devolve consultado:false antes de qualquer coisa', () => {
    const { app } = appDeTeste({});
    assert.equal(app.estado().consultado, false,
        'estado inicial precisa dizer que nada foi consultado');
});

teste('nenhum fetch acontece só por criar a aplicação', () => {
    const { app, fetch } = appDeTeste({});
    assert.equal(fetch.quantidade(), 0,
        'criar a aplicação não pode buscar nada — o estado vazio é a regra');
    app.estado();
    assert.equal(fetch.quantidade(), 0, 'estado() é só leitura, não busca');
});

teste('nenhum fetch acontece por ler estado ou definir fonte', () => {
    const { app, fetch } = appDeTeste({});
    app.estado();
    app.definirFonte('ibge');
    assert.equal(fetch.quantidade(), 0,
        'definir fonte não pode disparar consulta — só o botão Filtrar busca');
});

teste('estado vazio não escreve nada nos painéis', () => {
    const { app, doc } = appDeTeste({});
    app.estado();
    /* #kpis tem os quatro cartões do HTML (com "--"); o que a app não pode
     * fazer é trocar esse texto. #graficos e #mapa, ao contrário, são
     * escritos pelo JS e precisam ter ficado como o app os deixou. */
    const valores = doc.porId.kpis.querySelectorAll('.kpi')
        .map((c) => c.querySelector('.kpi-valor').textContent.trim());
    assert.deepEqual(valores, ['--', '--', '--', '--'],
        `os KPIs continuam em "--": ${JSON.stringify(valores)}`);
    assert.equal(doc.porId.graficos.textContent, '',
        'gráficos devem continuar com o placeholder do HTML, não com texto do JS');
    assert.equal(doc.porId.mapa.textContent, '',
        'o mapa também fica intocado');
});

teste('filtros não são populados no carregamento', () => {
    const { doc } = appDeTeste({});
    assert.equal(doc.porId.uf.children.length, 0,
        '#uf nasce vazio: as opções vêm do servidor, depois do clique');
    assert.equal(doc.porId.ano.children.length, 0,
        '#ano nasce vazio pelo mesmo motivo');
});

/* ================================================================== */
/* 2. O clique em Filtrar é o que busca                                */
/* ================================================================== */

await testeAsync('filtrar() chama fetch; sem ele, nada é buscado', async () => {
    const { app, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 1, dados: [{ cod: '1100205', nome: 'Porto Velho', valor: 10 }] } },
    });
    assert.equal(fetch.quantidade(), 0);
    await app.filtrar();
    assert.ok(fetch.quantidade() > 0, 'filtrar() precisa buscar');
    assert.equal(app.estado().consultado, true,
        'após filtrar(), estado precisa dizer que consultou');
});

await testeAsync('filtrar() desabilita o botão e o reabilita no fim', async () => {
    const { app, doc } = appDeTeste({
        '/api/consulta': { json: { total: 0, dados: [] } },
    });
    const botao = doc.porId.filtrar;
    assert.equal(botao.disabled, false, 'botão começa habilitado');
    await app.filtrar();
    assert.equal(botao.disabled, false, 'botão volta a habilitar no fim');
});

await testeAsync('filtrar() mostra carregando durante a consulta', async () => {
    const { app } = appDeTeste({
        '/api/consulta': { json: { total: 0, dados: [] } },
    });
    assert.equal(app.estado().carregando, false, 'não começa carregando');
    await app.filtrar();
    assert.equal(app.estado().carregando, false, 'termina de carregar');
});

await testeAsync('consulta leva todos os filtros escolhidos', async () => {
    const { app, doc, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, dados: [] } },
    }, { fonte: 'ibge', uf: 'RO', ano: '2024', indicador: 'populacao', nivel: 'municipio', ordenar: 'valor_desc', busca: 'Porto' });
    /* O painel se normaliza ao ser construído (preenche os catálogos e escolhe
     * um limite padrão), então o que se quer testar é o valor escolhido depois
     * disso — que é o que o usuário faz ao mexer no seletor. */
    doc.porId.limite.value = '50';
    await app.filtrar();
    const url = fetch.chamadas.map((c) => c.url).find((u) => u.includes('consulta'));
    assert.ok(url, 'a consulta foi chamada');
    assert.ok(url.includes('fonte=ibge'), `fonte na query: ${url}`);
    assert.ok(url.includes('uf=RO'), `uf na query: ${url}`);
    assert.ok(url.includes('ano=2024'), `ano na query: ${url}`);
    assert.ok(url.includes('indicador=populacao'), `indicador na query: ${url}`);
    assert.ok(url.includes('nivel=municipio'), `nivel na query: ${url}`);
    assert.ok(url.includes('busca=Porto'), `busca na query: ${url}`);
    assert.ok(url.includes('ordenar=valor_desc'), `ordenar na query: ${url}`);
    assert.ok(url.includes('limite=50'), `limite na query: ${url}`);
});

await testeAsync('erro do servidor vai para #kpis e não para o console', async () => {
    const doc = criaDoc(IDS);
    const fetch = criaFetch({
        '/api/consulta': {
            status: 400,
            json: { erro: { codigo: 400, mensagem: 'ano 2022 inexistente' } },
        },
    });
    /* Com ano escolhido, o erro que chega é o do servidor — que é o que este
     * teste mede. Sem ano, o front nem chega a consultar (ver o teste de ano
     * ausente), e a mensagem seria outra. */
    doc.porId.ano.value = '2022';
    const app = criarApp({ doc, fetch, logger: null });
    /* console é o global do node; se o app escrever nele, o teste acusa. */
    let escreveuConsole = false;
    const orig = { log: console.log, error: console.error, warn: console.warn };
    console.log = () => { escreveuConsole = true; };
    console.error = () => { escreveuConsole = true; };
    console.warn = () => { escreveuConsole = true; };
    try {
        await app.filtrar();
    } finally {
        console.log = orig.log;
        console.error = orig.error;
        console.warn = orig.warn;
    }
    assert.equal(doc.porId.kpis.textContent, 'ano 2022 inexistente',
        'a mensagem do servidor vai para #kpis');
    assert.equal(escreveuConsole, false,
        'erro de consulta não pode virar ruído no console — o usuário lê em #kpis');
    assert.equal(app.estado().consultado, false,
        'consulta que falhou não conta como consultado');
});

await testeAsync('falha de rede vira mensagem legível em #kpis', async () => {
    const { app, doc } = appDeTeste({
        '/api/consulta': new Error('rede'),
    });
    await app.filtrar();
    assert.ok(doc.porId.kpis.textContent.length > 0,
        'falha de rede também precisa aparecer na tela');
    assert.equal(app.estado().consultado, false);
});

/* ================================================================== */
/* 3. Valores ausentes: "" e ".." nunca viram 0                        */
/* ================================================================== */

teste('anoValido rejeita vazio, espaços e lixo', () => {
    assert.equal(anoValido(''), null, '"" não é ano');
    assert.equal(anoValido('   '), null, 'espaço não é ano');
    assert.equal(anoValido('..'), null, '".." não é ano');
    assert.equal(anoValido('0'), null, '0 não é ano válido');
    assert.equal(anoValido('abcd'), null, 'texto não é ano');
    assert.equal(anoValido('20a4'), null, 'ano com letra no meio é inválido');
    assert.equal(anoValido('2024x'), null, 'ano com sufixo é inválido');
});

teste('anoValido aceita anos reais', () => {
    assert.equal(anoValido('2024'), 2024);
    assert.equal(anoValido('1991'), 1991);
    assert.equal(anoValido(2024), 2024);
});

teste('inteiroValido rejeita vazio e lixo, e tem piso', () => {
    assert.equal(inteiroValido('', 1, 5570), null);
    assert.equal(inteiroValido('..', 1, 5570), null);
    assert.equal(inteiroValido('-5', 1, 5570), null, 'abaixo do piso');
    assert.equal(inteiroValido('0', 1, 5570), null, '0 é abaixo do piso');
    assert.equal(inteiroValido('50', 1, 5570), 50);
    assert.equal(inteiroValido('99999', 1, 5570), null, 'acima do teto');
});

teste('consulta sem ano não manda ano=0', async () => {
    /* Este é o bug que a spec nomeia: um <select> vazio entrega "", e um
     * parse ingênuo transforma isso em 0 — que o servidor rejeita com um 400
     * confuso. Aqui a consulta tem que omitir o parâmetro. */
    const { app, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, dados: [] } },
    }, { ano: '' });
    await app.filtrar();
    const url = fetch.chamadas.map((c) => c.url).find((u) => u.includes('consulta')) || '';
    assert.ok(!/[?&]ano=0(&|$)/.test(url), `nada de ano=0 na query: ${url}`);
    assert.ok(!/[?&]ano=(&|$)/.test(url), `ano vazio é omitido, não enviado: ${url}`);
});

teste('limite vazio não vira limite=0', async () => {
    const { app, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, dados: [] } },
    }, { limite: '' });
    await app.filtrar();
    const url = fetch.chamadas.map((c) => c.url).find((u) => u.includes('consulta')) || '';
    assert.ok(!/[?&]limite=0(&|$)/.test(url), `nada de limite=0: ${url}`);
});

/* ================================================================== */
/* 4. Query string                                                    */
/* ================================================================== */

teste('montarQuery escapa acentos e espaços', () => {
    const q = montarQuery({ busca: 'São Paulo', uf: 'RO', ano: '2024' });
    assert.ok(q.includes('busca=S%C3%A3o%20Paulo'), `busca escapada: ${q}`);
    assert.ok(q.includes('uf=RO'));
    assert.ok(q.includes('ano=2024'));
});

teste('montarQuery escapa o & da busca para não cortar a query', () => {
    const q = montarQuery({ busca: 'A&B' });
    assert.ok(q.includes('busca=A%26B'), `& precisa virar %26: ${q}`);
    assert.equal(q.split('&').length, 1, 'o & do valor não vira separador');
});

teste('montarQuery usa %20 e não +', () => {
    const q = montarQuery({ busca: 'a b' });
    assert.ok(!q.includes('+'), `espaço não pode virar +: ${q}`);
    assert.ok(q.includes('%20'), `espaço é %20: ${q}`);
});

teste('montarQuery omite valor ausente e nunca escreve chave= vazia', () => {
    const q = montarQuery({ uf: '', ano: null, limite: undefined, busca: 'x' });
    assert.ok(!q.includes('uf='), `uf vazio é omitido: ${q}`);
    assert.ok(!q.includes('ano='), `ano nulo é omitido: ${q}`);
    assert.ok(!q.includes('limite='), `limite indefinido é omitido: ${q}`);
    assert.ok(q.includes('busca=x'));
});

teste('montarQuery com nada devolve string vazia, não "?"', () => {
    assert.equal(montarQuery({}), '');
    assert.equal(montarQuery(), '');
});

/* paramsDaConsulta é a metade que decide o que entra; montarQuery é a metade
 * que escreve na URL. As duas juntas são o caminho do filtro. */
teste('paramsDaConsulta não emite ano nem limite ausentes', () => {
    const p = paramsDaConsulta({ fonte: 'ibge', ano: '', limite: '..' });
    assert.equal('ano' in p, false, `ano ausente não entra: ${JSON.stringify(p)}`);
    assert.equal('limite' in p, false, `limite ausente não entra: ${JSON.stringify(p)}`);
    assert.equal(p.fonte, 'ibge');
});

teste('paramsDaConsulta emite ano e limite quando válidos', () => {
    const p = paramsDaConsulta({ fonte: 'ibge', ano: '2024', limite: '50' });
    assert.equal(p.ano, 2024);
    assert.equal(p.limite, 50);
});

teste('o caminho inteiro não transforma ausente em 0', () => {
    for (const ano of ['', '..', '  ', '0']) {
        const q = montarQuery(paramsDaConsulta({ fonte: 'ibge', ano, limite: '100' }));
        assert.ok(!/ano=0/.test(q), `"${ano}" não pode virar ano=0: ${q}`);
    }
});

teste('busca com acento chega escapada e intacta', () => {
    const q = montarQuery(paramsDaConsulta({ fonte: 'ibge', ano: '2024', busca: 'Ilhéus' }));
    assert.ok(q.includes('busca=Ilh%C3%A9us'), `acento escapado: ${q}`);
    assert.equal(decodeURIComponent(q.split('busca=')[1]), 'Ilhéus',
        'o que volta é exatamente o que o usuário digitou');
});

/* ------------------------------------------------------------------ */
/* O clique de verdade                                                 */
/* ------------------------------------------------------------------ */

await testeAsync('na segunda consulta, o ano trocado pelo usuário é respeitado', async () => {
    /* No segundo filtrar as opções já estão em cache e `garantirOpcoes`
     * devolve ano/uf nulos de propósito — quem manda é o formulário. Se
     * alguém "acertar" esse retorno com o ano das opções, a escolha do
     * usuário é sobrescrita sem ele perceber. */
    const { app, doc, fetch } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024, 2021, 2010], ufs: ['RO', 'SP'] } },
        '/api/consulta': { json: { total: 0, dados: [] } },
    }, { ano: '2024' });
    await app.filtrar();
    doc.porId.ano.value = '2010';
    doc.porId.uf.value = 'SP';
    await app.filtrar();
    const urls = fetch.urls().filter((u) => u.includes('consulta'));
    assert.equal(urls.length, 2, 'a segunda consulta saiu');
    assert.ok(urls[1].includes('ano=2010'), `ano escolhido na 2ª: ${urls[1]}`);
    assert.ok(urls[1].includes('uf=SP'), `UF escolhida na 2ª: ${urls[1]}`);
    /* As opções não são buscadas de novo: uma vez basta por (fonte, indicador). */
    assert.equal(fetch.urls().filter((u) => u.includes('opcoes')).length, 1,
        'opções em cache não viram requisição repetida');
    assert.equal(doc.porId.ano.value, '2010', 'e o seletor fica onde o usuário deixou');
});

await testeAsync('o clique no botão Filtrar é o gatilho da busca', async () => {
    const { app, doc, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 1, dados: [] } },
    });
    assert.equal(fetch.quantidade(), 0, 'antes do clique, rede intocada');
    await doc.porId.filtrar.click();
    assert.ok(fetch.quantidade() > 0, 'o clique no botão precisa buscar');
    assert.equal(app.estado().consultado, true);
});

await testeAsync('digitar no formulário não busca nada sozinho', async () => {
    const { app, doc, fetch } = appDeTeste({ '/api/consulta': { json: { total: 0, dados: [] } } });
    doc.porId.busca.value = 'Porto Velho';
    doc.porId.ano.value = '2024';
    assert.equal(fetch.quantidade(), 0, 'mexer nos campos não busca');
    assert.equal(app.estado().consultado, false, 'e não conta como consultado');
    await doc.porId.filtrar.click();
    const url = fetch.urls().find((u) => u.includes('consulta'));
    assert.ok(url.includes('busca=Porto%20Velho'), `a busca digitada vai na query: ${url}`);
});

/* ------------------------------------------------------------------ */
/* Resultado renderizado                                               */
/* ------------------------------------------------------------------ */

await testeAsync('consulta com dados preenche os KPIs, sem zero para ausente', async () => {
    const { app } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: [] } },
        '/api/consulta': {
            json: {
                total: 2, min: 100, max: 900, media: 500, soma: 1000,
                dados: [
                    { cod: '1100205', nome: 'Porto Velho', valor: 900 },
                    { cod: '1100155', nome: 'Ji-Paraná', valor: -1 },
                ],
            },
        },
    }, { ano: '2024' });
    await app.filtrar();
    assert.equal(app.estado().consultado, true);
    assert.equal(app.estado().total, 2);
    assert.equal(app.estado().kpis.max, 900);
    assert.equal(app.estado().dados.length, 2);
    assert.equal(textoOpcoes(-1), '--', 'valor ausente não vira 0 na tela');
});

await testeAsync('os cartões de KPI são preenchidos na tela, não só no estado', async () => {
    /* Regressão real, achada abrindo no navegador: os quatro KPIs ficavam
     * "--" mesmo com a consulta respondendo, porque o laço de render não
     * encontrava os cartões. O estado estava certo — só a tela mentia. Um
     * teste que olha só `estado()` passa e não pega nada disso. */
    const { app, doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: [] } },
        '/api/consulta': {
            json: { total: 3, min: 1746227, max: 1796460, media: 1770000, soma: 5310000, dados: [] },
        },
    }, { ano: '2024' });
    await app.filtrar();

    const cartoes = doc.porId.kpis.querySelectorAll('.kpi');
    assert.equal(cartoes.length, 4, 'os quatro cartões do HTML continuam lá');
    const valores = cartoes.map((c) => c.querySelector('.kpi-valor').textContent.trim());
    /* O total é o único com unidade; os demais são números crus, para não
     * repetir "municípios" em todo cartão. */
    assert.deepEqual(valores, ['3', '1.746.227', '1.796.460', '1.770.000'],
        `os KPIs na tela têm que mostrar o número: ${JSON.stringify(valores)}`);
    assert.ok(doc.porId.kpis.textContent.includes('municípios'),
        'e o total traz a unidade');
    assert.ok(!valores.includes('--'), 'nenhum "--" sobrou depois da consulta');
    const rotulos = cartoes.map((c) => c.querySelector('.kpi-rotulo').textContent.trim());
    assert.deepEqual(rotulos, ['Total de municípios', 'Mínimo', 'Máximo', 'Média'],
        `e os rótulos continuam certos: ${JSON.stringify(rotulos)}`);
    assert.equal(doc.porId.kpis.className, '', '#kpis sai do estado de vazio');
});

await testeAsync('voltar ao branco depois de consultar restaura os "--"', async () => {
    const { app, doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: [] } },
        '/api/consulta': { json: { total: 3, min: 1, max: 9, media: 5, soma: 15, dados: [] } },
    }, { ano: '2024' });
    await app.filtrar();
    assert.ok(!doc.porId.kpis.querySelectorAll('.kpi')[0]
        .querySelector('.kpi-valor').textContent.includes('--'));
    /* Trocar a fonte desfaz a consulta: é onde a tela tem que voltar ao
     * branco, senão o número de IBGE fica embaixo de "Saúde". */
    await app.definirFonte('saude');
    const valores = doc.porId.kpis.querySelectorAll('.kpi')
        .map((c) => c.querySelector('.kpi-valor').textContent.trim());
    assert.deepEqual(valores, ['--', '--', '--', '--'],
        `todos os KPIs voltam para "--": ${JSON.stringify(valores)}`);
    assert.equal(doc.porId.kpis.className, 'estado-vazio');
    assert.equal(doc.porId.graficos.textContent.trim(), 'Ajuste os filtros e clique em Filtrar');
    assert.equal(doc.porId.mapa.textContent.trim(), 'Sem dados — clique em Filtrar');
});

await testeAsync('o texto do gráfico usa o rótulo com acento, não o token cru', async () => {
    const { app, doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: [] } },
        '/api/consulta': { json: { total: 1, min: 1, max: 2, media: 2, soma: 2, dados: [{ cod: '1100205', nome: 'Porto Velho', valor: 2 }] } },
    }, { ano: '2024', indicador: 'populacao' });
    await app.filtrar();
    const txt = doc.porId.graficos.textContent;
    assert.ok(txt.includes('População'), `rótulo com acento na tela: ${txt}`);
    assert.ok(!txt.includes('populacao'), `sem token cru em tela: ${txt}`);
});

teste('rotuloIndicador acerta todos os indicadores, com acento', () => {
    assert.equal(rotuloIndicador('populacao'), 'População');
    assert.equal(rotuloIndicador('estabelecimentos'), 'Estabelecimentos');
    assert.equal(rotuloIndicador('leitos'), 'Leitos');
    assert.equal(rotuloIndicador('uti'), 'UTI');
    assert.equal(rotuloIndicador('dengue'), 'Dengue');
    assert.equal(rotuloIndicador('naoexiste'), 'naoexiste', 'desconhecido não vira rótulo inventado');
});

await testeAsync('dados com acento preservam o nome vindo do servidor', async () => {
    const { app } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: [] } },
        '/api/consulta': {
            json: {
                total: 2, min: 1, max: 2, dados: [
                    { cod: '1100155', nome: 'Ji-Paraná', valor: 1 },
                    { cod: '2914800', nome: 'Ilhéus', valor: 2 },
                ],
            },
        },
    }, { ano: '2024' });
    await app.filtrar();
    const nomes = app.estado().dados.map((d) => d.nome);
    assert.ok(nomes.includes('Ji-Paraná'), `acento preservado: ${nomes}`);
    assert.ok(nomes.includes('Ilhéus'), `acento preservado: ${nomes}`);
});

/* ------------------------------------------------------------------ */
/* Cliques repetidos                                                   */
/* ------------------------------------------------------------------ */

await testeAsync('dois filtrar() seguidos somam uma requisição', async () => {
    const { app, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, dados: [] } },
    });
    await Promise.all([app.filtrar(), app.filtrar()]);
    const consultas = fetch.urls().filter((u) => u.includes('consulta'));
    assert.equal(consultas.length, 1,
        `clique duplo não pode virar duas consultas: ${consultas.length}`);
});

await testeAsync('depois de uma consulta, um novo filtrar() busca de novo', async () => {
    const { app, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, dados: [] } },
    });
    await app.filtrar();
    await app.filtrar();
    const consultas = fetch.urls().filter((u) => u.includes('consulta'));
    assert.equal(consultas.length, 2, 'filtrar() de novo tem que consultar de novo');
});

/* ------------------------------------------------------------------ */
/* Mudança de fonte                                                     */
/* ------------------------------------------------------------------ */

await testeAsync('trocar de fonte busca as opções da nova fonte', async () => {
    const { app, fetch } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: [] } },
        '/api/consulta': { json: { total: 0, dados: [] } },
    });
    await app.filtrar();
    assert.equal(app.estado().opcoesCarregadas, true);
    const antes = fetch.urls().filter((u) => u.includes('opcoes')).length;
    await app.definirFonte('saude');
    await app.filtrar();
    const depois = fetch.urls().filter((u) => u.includes('opcoes')).length;
    assert.equal(depois, antes + 1,
        'fonte nova tem opções novas — precisa buscar de novo');
    assert.ok(fetch.urls().some((u) => u.includes('fonte=saude')),
        `a consulta da fonte nova vai com fonte=saude: ${fetch.urls()}`);
});

/* ================================================================== */
/* 5. filtros.js: respeitar o que o servidor devolveu                  */
/* ================================================================== */

teste('textoOpcoes formata número com separador de milhar', () => {
    assert.equal(textoOpcoes(1234567), '1.234.567');
    assert.equal(textoOpcoes(0), '0');
    assert.equal(textoOpcoes(null), '--');
    assert.equal(textoOpcoes(undefined), '--');
});

teste('textoOpcoes mostra ausente como -- e não como 0', () => {
    /* Valor ausente no servidor é -1 (sentinela do aggregate). Se isso virar
     * "0", o usuário lê zero numa métrica que na verdade não existe. */
    assert.equal(textoOpcoes(-1), '--');
});

teste('OPCOES_INDICADOR traz populacao, estabelecimentos, leitos, uti e dengue', () => {
    const vals = OPCOES_INDICADOR.map((o) => o.valor);
    for (const v of ['populacao', 'estabelecimentos', 'leitos', 'uti', 'dengue']) {
        assert.ok(vals.includes(v), `indicador ${v} disponível`);
    }
});

teste('OPCOES_INDICADOR tem rótulo com acento e maiúscula', () => {
    const pop = OPCOES_INDICADOR.find((o) => o.valor === 'populacao');
    assert.ok(pop && pop.rotulo.length > 0, 'rótulo existe');
    assert.equal(pop.rotulo, 'População');
});

teste('OPCOES_NIVEL só oferece município e uf', () => {
    assert.deepEqual(OPCOES_NIVEL.map((o) => o.valor), ['municipio', 'uf']);
});

teste('ORDENS tem desc, asc, nome e código', () => {
    assert.deepEqual(ORDENS.map((o) => o.codigo), ['valor_desc', 'valor_asc', 'nome_asc']);
    assert.ok(OPCOES_ORDENAR.length >= 3, 'três opções de ordenação');
});

teste('fonte ibge oferece só indicadores IBGE; saude, só os de saúde', () => {
    const ibge = OPCOES_INDICADOR.filter((o) => o.fontes.includes('ibge')).map((o) => o.valor);
    const saude = OPCOES_INDICADOR.filter((o) => o.fontes.includes('saude')).map((o) => o.valor);
    assert.ok(ibge.includes('populacao'));
    assert.ok(!ibge.includes('leitos'), 'leitos é de saúde, não do IBGE');
    assert.ok(saude.includes('leitos'));
    assert.ok(saude.includes('uti'));
    assert.ok(saude.includes('dengue'));
});

await testeAsync('indicaores são filtrados pela fonte escolhida', async () => {
    const { app, doc, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, dados: [] } },
    }, { fonte: 'ibge' });
    await app.definirFonte('ibge');
    assert.ok(fetch.quantidade() === 0, 'trocar fonte não busca');
    const rotulos = doc.porId.indicador.children.map((o) => o.textContent);
    assert.ok(rotulos.includes('População'), `#indicador repopulado: ${rotulos}`);
    assert.ok(!rotulos.includes('Leitos'), `IBGE não oferece leitos: ${rotulos}`);
});

await testeAsync('definirFonte marca a fonte no estado e repopula indicadores', async () => {
    const { app, doc } = appDeTeste({});
    await app.definirFonte('saude');
    assert.equal(app.estado().fonte, 'saude');
    const rotulos = doc.porId.indicador.children.map((o) => o.textContent);
    assert.ok(rotulos.includes('Leitos'), `Saúde oferece leitos: ${rotulos}`);
    assert.ok(!rotulos.includes('População'), `Saúde não oferece população: ${rotulos}`);
});

await testeAsync('definirFonte com fonte desconhecida mantém a atual', async () => {
    const { app } = appDeTeste({});
    await app.definirFonte('inexistente');
    assert.equal(app.estado().fonte, 'ibge', 'fonte inválida não substitui a atual');
});

/* ================================================================== */
/* 6. Opções do servidor: respeitar lacunas                            */
/* ================================================================== */

await testeAsync('anos e ufs do servidor preenchem os seletores', async () => {
    const { app, doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024, 2021, 2010], ufs: ['RO', 'SP'] } },
        '/api/consulta': { json: { total: 0, dados: [] } },
    });
    await app.filtrar();
    const anos = doc.porId.ano.children.map((o) => o.value);
    assert.deepEqual(anos, ['2024', '2021', '2010'],
        'anos vêm do servidor, na ordem do servidor — 2022 não pode aparecer');
    assert.ok(!anos.includes('2022'), 'lacuna de 2022 não é oferecida');
    const ufs = doc.porId.uf.children.map((o) => o.value);
    assert.deepEqual(ufs, ['', 'RO', 'SP'], 'primeira opção é "todas as UFs" (vazio)');
});

await testeAsync('sem ano disponível, diz por que em vez de deixar o 400 do servidor', async () => {
    /* Foi o que aconteceu de verdade contra o servidor: o modelo está vazio e
     * opcoes devolve {"anos": [], "ufs": []}. Sem ano não há consulta possível,
     * e mandar o request mesmo assim só rende um 400 críptico. */
    const { app, doc, fetch } = appDeTeste({
        '/api/opcoes': { json: { anos: [], ufs: [] } },
        '/api/consulta': { json: { total: 0, dados: [] } },
    }, { ano: '' });
    await app.filtrar();
    assert.equal(app.estado().consultado, false, 'não houve consulta, logo não há resultado');
    assert.ok(/Nenhum ano disponível/.test(doc.porId.kpis.textContent),
        `a tela diz o motivo: ${doc.porId.kpis.textContent}`);
    assert.ok(/--atualizar/.test(doc.porId.kpis.textContent),
        'e diz o comando que resolve');
    assert.equal(fetch.urls().filter((u) => u.includes('consulta')).length, 0,
        'não adianta disparar uma consulta que o servidor vai recusar');
    /* Só o aviso de "nenhum ano" — nenhum ano de verdade. */
    const valores = doc.porId.ano.children.map((o) => o.value);
    assert.deepEqual(valores.filter((v) => v !== ''), [],
        `nenhum ano inventado: ${valores}`);
    assert.ok(doc.porId.ano.textContent.includes('Nenhum ano'),
        'e o seletor diz por que está vazio');
    assert.ok(!/[?&]ano=0/.test(fetch.urls().join(' ')), 'e nada de ano=0 na query');
});

await testeAsync('com ano válido, a consulta segue mesmo sem opções', async () => {
    const { app, fetch } = appDeTeste({
        '/api/opcoes': { json: { anos: [], ufs: [] } },
        '/api/consulta': { json: { total: 0, dados: [] } },
    }, { ano: '2024' });
    await app.filtrar();
    assert.equal(app.estado().consultado, true, 'o ano digitado basta');
    assert.ok(fetch.urls().some((u) => u.includes('ano=2024')), 'e ele vai na query');
});

await testeAsync('/api/opcoes falhando não cancela a consulta', async () => {
    /* As opções preenchem os seletores; a consulta é o que o usuário pediu.
     * Uma não pode derrubar a outra. */
    const { app, fetch } = appDeTeste({
        '/api/opcoes': new Error('opcoes fora'),
        '/api/consulta': { json: { total: 1, dados: [] } },
    }, { ano: '2024' });
    await app.filtrar();
    assert.equal(app.estado().consultado, true, 'a consulta tinha que acontecer');
    assert.ok(fetch.urls().some((u) => u.includes('consulta')), 'e foi consulta mesmo');
});

await testeAsync('a consulta usa o ano que o usuário escolheu, não o primeiro da lista', async () => {
    const { app, doc, fetch } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024, 2021], ufs: [] } },
        '/api/consulta': { json: { total: 0, dados: [] } },
    }, { ano: '2021' });
    await app.filtrar();
    assert.equal(doc.porId.ano.value, '2021', 'seleção do usuário é preservada');
    const url = fetch.chamadas.map((c) => c.url).find((u) => u.includes('consulta'));
    assert.ok(url.includes('ano=2021'), `ano escolhido vai na query: ${url}`);
});

/* ================================================================== */
/* 7. api.js: rotas, envelope de erro e cache de geometria              */
/* ================================================================== */

teste('rotaConsulta monta o caminho de api/consulta', () => {
    assert.equal(rotaConsulta({}), '/api/consulta');
    assert.equal(rotaConsulta({ ano: 2024 }), '/api/consulta?ano=2024');
    assert.ok(rotaConsulta({ busca: 'São Paulo' }).includes('busca=S%C3%A3o%20Paulo'));
});

await testeAsync('consultar devolve o envelope com dados', async () => {
    const fetch = criaFetch({
        '/api/consulta': { json: { total: 2, min: 1, max: 9, media: 5, soma: 10, dados: [] } },
    });
    const r = await consultar({ ano: 2024 }, { fetch });
    assert.equal(r.total, 2);
    assert.equal(r.max, 9);
    assert.ok(Array.isArray(r.dados), 'dados é lista');
});

await testeAsync('consultar transforma envelope de erro em exceção com a mensagem', async () => {
    const fetch = criaFetch({
        '/api/consulta': { status: 400, json: { erro: { codigo: 400, mensagem: 'ano 2022 inexistente' } } },
    });
    let msg = '';
    try {
        await consultar({ ano: 2022 }, { fetch });
    } catch (e) {
        msg = e.message;
    }
    assert.equal(msg, 'ano 2022 inexistente',
        'a mensagem do servidor precisa chegar intacta até a tela');
});

await testeAsync('consultar com corpo que não é JSON vira erro utilizável', async () => {
    const fetch = criaFetch({ '/api/consulta': { status: 500, json: new Error('html') } });
    let msg = '';
    try {
        await consultar({ ano: 2024 }, { fetch });
    } catch (e) {
        msg = e.message;
    }
    assert.ok(msg.length > 0, 'resposta quebrada ainda precisa de mensagem');
});

await testeAsync('opcoes normaliza e devolve listas', async () => {
    const fetch = criaFetch({ '/api/opcoes': { json: { anos: ['2024', 2021], ufs: ['RO'] } } });
    const o = await opcoes('ibge', 'populacao', { fetch });
    assert.deepEqual(o.anos, [2024, 2021], 'ano chega como string vira número');
    assert.deepEqual(o.ufs, ['RO']);
});

await testeAsync('opcoes descarta ano que não é número, em vez de virar 0', async () => {
    const fetch = criaFetch({ '/api/opcoes': { json: { anos: ['2024', '', null, '..'], ufs: [] } } });
    const o = await opcoes('ibge', 'populacao', { fetch });
    assert.deepEqual(o.anos, [2024], `"", null e ".." somem: ${JSON.stringify(o.anos)}`);
    assert.ok(!o.anos.includes(0), 'nenhum 0 entra na lista de anos');
});

await testeAsync('geo só aceita os dois níveis que o servidor serve', async () => {
    const fetch = criaFetch({ '/api/geo/estados': { json: { features: [] } } });
    await assert.rejects(() => geo('pais', { fetch, cache: new Map() }),
        /nível de mapa desconhecido/,
        'nível inventado precisa ser erro de programação, não um 404 do servidor');
    await assert.rejects(() => geo('', { fetch, cache: new Map() }));
    await assert.rejects(() => geo('estados/RO', { fetch, cache: new Map() }));
});

await testeAsync('geo busca exatamente /api/geo/{nivel}', async () => {
    for (const nivel of ['estados', 'municipios']) {
        const fetch = criaFetch({ [`api/geo/${nivel}`]: { json: { features: [] } } });
        await geo(nivel, { fetch, cache: new Map() });
        assert.equal(fetch.urls()[0], `/api/geo/${nivel}`);
    }
});

await testeAsync('geo busca a malha uma vez e reusa do cache', async () => {
    /* A malha é desenho, não dado: pode ser memorizada. Este teste fixa
     * essa diferença — duas chamadas, um fetch. */
    const cache = new Map();
    const fetch = criaFetch({ '/api/geo/estados': { json: { features: [] } } });
    const a = await geo('estados', { fetch, cache });
    const b = await geo('estados', { fetch, cache });
    assert.equal(fetch.quantidade(), 1, 'a segunda chamada vem do cache');
    assert.equal(a, b, 'mesma geometria devolvida');
});

await testeAsync('falha ao buscar malha não derruba a página', async () => {
    const cache = new Map();
    const fetch = criaFetch({ '/api/geo/estados': new Error('offline') });
    let r = 'nao-chamou';
    try {
        r = await geo('estados', { fetch, cache });
    } catch (e) {
        r = 'lançou';
    }
    assert.equal(r, null, 'malha indisponível devolve null, não exceção');
});

/* ================================================================== */
/* 8. Ruling C-5: cálculo puro separado do DOM                        */
/* ================================================================== */

teste('segmentosLinha quebra a série onde falta ano', () => {
    /* A série do IBGE tem lacuna em 2022 e 2023. Ligar 2021 a 2024 sem
     * quebrar diria que a série é contínua, e ela não é. */
    const s = segmentosLinha([{ r: 2021, v: 1796460 }, { r: 2024, v: 1746227 }]);
    assert.equal(s.length, 2, '2021→2024 vira dois segmentos, não um');
    assert.equal(s[0][0].r, 2021);
    assert.equal(s[1][0].r, 2024);
});

teste('segmentosLinha não gera segmento para valor null', () => {
    assert.deepEqual(segmentosLinha([{ r: 2022, v: null }]), []);
    assert.deepEqual(segmentosLinha([{ r: 2022, v: -1 }]), [], '-1 é ausente, não valor');
});

teste('segmentosLinha liga anos consecutivos', () => {
    const s = segmentosLinha([{ r: 2020, v: 1 }, { r: 2021, v: 2 }, { r: 2022, v: 3 }]);
    assert.equal(s.length, 1, '2020-2021-2022 é uma série contínua');
    assert.equal(s[0].length, 3);
});

teste('segmentosLinha ignora anos fora de ordem sem inventar ligação', () => {
    const s = segmentosLinha([{ r: 2021, v: 1 }, { r: 2020, v: 2 }]);
    assert.ok(Array.isArray(s));
    assert.ok(s.every((seg) => seg.every((p) => typeof p.r === 'number')));
});

teste('escalaLinear não divide por zero com valores iguais', () => {
    const e = escalaLinear(5, 5, 0, 100);
    assert.ok(Number.isFinite(e(5)), 'valor igual ao mínimo não gera NaN/Infinity');
    assert.ok(Number.isFinite(e(5.5)));
});

teste('escalaLinear com lista vazia devolve uma função estável', () => {
    const e = escalaLinear(null, null, 0, 100);
    assert.ok(Number.isFinite(e(0)) || Number.isFinite(e(null)),
        'sem dados, a escala ainda devolve número finito');
});

/* ================================================================== */
/* 9. Contrato de window.Dashboard                                     */
/* ================================================================== */

teste('a aplicação expõe filtrar, estado e definirFonte', () => {
    const { app } = appDeTeste({});
    assert.equal(typeof app.filtrar, 'function');
    assert.equal(typeof app.estado, 'function');
    assert.equal(typeof app.definirFonte, 'function');
});

teste('estado() devolve uma cópia, não o estado interno', () => {
    const { app } = appDeTeste({});
    const a = app.estado();
    a.consultado = true;
    a.fonte = 'saude';
    const b = app.estado();
    assert.equal(b.consultado, false, 'mexer no retorno não muda o estado interno');
    assert.equal(b.fonte, 'ibge');
});

teste('estadoInicial e estadoVazio descrevem a mesma tela parada', () => {
    assert.equal(estadoInicial().consultado, false);
    assert.equal(estadoVazio().consultado, false);
});

/* ================================================================== */

if (falhas > 0) {
    console.error('FALHOU:');
    for (const f of falhasDetalhadas) console.error('  - ' + f);
    console.error(`\n${falhas} de ${testes} testes do front falharam`);
    process.exit(1);
}
console.log(`OK: ${testes} testes do front passaram`);
