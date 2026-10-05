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
import { criaDoc, docDeTeste, IDS } from './fake_dom.mjs';
import {
    segmentosLinha,
    escalaLinear,
    dominioValores,
    fatiasDonut,
    desenharLinha,
    paresDaSerie,
    geometriaLinha,
    geometriaBarras,
    geometriaDonut,
    publicacaoDoAno,
    rotulosDoEixoX,
    coresCategoricas,
    corSequencial,
    abreviaNumero,
    encurtaTexto,
    linha,
    barras,
    donut,
    kpi,
    valorPresente,
    quatroTicks,
    COR_AUSENCIA,
} from '../web/charts.js';
import { plural, preencherOpcoes } from '../web/filters.js';

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

function appDeTeste(respostas, valores = {}) {
    /* O #kpis do index.html tem quatro cartões; reproduzir aqui é o que
     * permite testar a renderização dos KPIs sem navegador. */
    const doc = docDeTeste(valores);
    const fetch = criaFetch(respostas);
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
        '/api/consulta': { json: { total: 1, registros: [{ cod: '1100205', nome: 'Porto Velho', valor: 10 }] } },
    });
    assert.equal(fetch.quantidade(), 0);
    await app.filtrar();
    assert.ok(fetch.quantidade() > 0, 'filtrar() precisa buscar');
    assert.equal(app.estado().consultado, true,
        'após filtrar(), estado precisa dizer que consultou');
});

await testeAsync('filtrar() desabilita o botão e o reabilita no fim', async () => {
    const { app, doc } = appDeTeste({
        '/api/consulta': { json: { total: 0, registros: [] } },
    });
    const botao = doc.porId.filtrar;
    assert.equal(botao.disabled, false, 'botão começa habilitado');
    await app.filtrar();
    assert.equal(botao.disabled, false, 'botão volta a habilitar no fim');
});

await testeAsync('filtrar() mostra carregando durante a consulta', async () => {
    const { app } = appDeTeste({
        '/api/consulta': { json: { total: 0, registros: [] } },
    });
    assert.equal(app.estado().carregando, false, 'não começa carregando');
    await app.filtrar();
    assert.equal(app.estado().carregando, false, 'termina de carregar');
});

await testeAsync('consulta leva todos os filtros escolhidos', async () => {
    const { app, doc, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': { json: { total: 0, registros: [] } },
    }, { ano: '' });
    await app.filtrar();
    const url = fetch.chamadas.map((c) => c.url).find((u) => u.includes('consulta')) || '';
    assert.ok(!/[?&]ano=0(&|$)/.test(url), `nada de ano=0 na query: ${url}`);
    assert.ok(!/[?&]ano=(&|$)/.test(url), `ano vazio é omitido, não enviado: ${url}`);
});

teste('limite vazio não vira limite=0', async () => {
    const { app, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': { json: { total: 1, registros: [] } },
    });
    assert.equal(fetch.quantidade(), 0, 'antes do clique, rede intocada');
    await doc.porId.filtrar.click();
    assert.ok(fetch.quantidade() > 0, 'o clique no botão precisa buscar');
    assert.equal(app.estado().consultado, true);
});

await testeAsync('digitar no formulário não busca nada sozinho', async () => {
    const { app, doc, fetch } = appDeTeste({ '/api/consulta': { json: { total: 0, registros: [] } } });
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
                kpis: { min: 100, max: 900, media: 500, soma: 1000, com_dado: 2 }, total: 2, registros: [
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
    assert.equal(app.estado().registros.length, 2);
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
            json: { kpis: { min: 1746227, max: 1796460, media: 1770000, soma: 5310000, com_dado: 3 }, total: 3, registros: [] },
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
        '/api/consulta': { json: { kpis: { min: 1, max: 9, media: 5, soma: 15, com_dado: 3 }, total: 3, registros: [] } },
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
        '/api/consulta': { json: { kpis: { min: 1, max: 2, media: 2, soma: 2, com_dado: 1 }, total: 1, registros: [{ cod: '1100205', nome: 'Porto Velho', valor: 2 }] } },
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
                kpis: { min: 1, max: 2, media: 1, soma: 3, com_dado: 2 },
                total: 2,
                registros: [
                    { cod: '1100155', nome: 'Ji-Paraná', valor: 1 },
                    { cod: '2914800', nome: 'Ilhéus', valor: 2 },
                ],
            },
        },
    }, { ano: '2024' });
    await app.filtrar();
    const nomes = app.estado().registros.map((d) => d.nome);
    assert.ok(nomes.includes('Ji-Paraná'), `acento preservado: ${nomes}`);
    assert.ok(nomes.includes('Ilhéus'), `acento preservado: ${nomes}`);
});

/* ------------------------------------------------------------------ */
/* Cliques repetidos                                                   */
/* ------------------------------------------------------------------ */

await testeAsync('dois filtrar() seguidos somam uma requisição', async () => {
    const { app, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, registros: [] } },
    });
    await Promise.all([app.filtrar(), app.filtrar()]);
    const consultas = fetch.urls().filter((u) => u.includes('consulta'));
    assert.equal(consultas.length, 1,
        `clique duplo não pode virar duas consultas: ${consultas.length}`);
});

await testeAsync('depois de uma consulta, um novo filtrar() busca de novo', async () => {
    const { app, fetch } = appDeTeste({
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': { json: { total: 1, registros: [] } },
    }, { ano: '2024' });
    await app.filtrar();
    assert.equal(app.estado().consultado, true, 'a consulta tinha que acontecer');
    assert.ok(fetch.urls().some((u) => u.includes('consulta')), 'e foi consulta mesmo');
});

await testeAsync('a consulta usa o ano que o usuário escolheu, não o primeiro da lista', async () => {
    const { app, doc, fetch } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024, 2021], ufs: [] } },
        '/api/consulta': { json: { total: 0, registros: [] } },
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
        '/api/consulta': {
            json: {
                consulta: { fonte: 'ibge', indicador: 'populacao', nivel: 'municipio', ano: 2024 },
                total: 2,
                kpis: { min: 1, max: 9, media: 5, soma: 10, com_dado: 2 },
                registros: [],
                serie: [1, null, 2],
                serie_anos: [2022, 2024, 2026],
            },
        },
    });
    const r = await consultar({ ano: 2024 }, { fetch });
    /* O envelope volta como veio — o front consome a spec §8, e `max` mora
     * dentro de `kpis`, não no topo. */
    assert.equal(r.total, 2);
    assert.equal(r.kpis.max, 9);
    assert.equal(r.consulta.ano, 2024, 'o eco da consulta volta');
    assert.equal(r.consulta.indicador, 'populacao');
    assert.ok(Array.isArray(r.registros), 'registros é lista');
    assert.deepEqual(r.serie, [1, null, 2], 'a série vem com null na lacuna');
    assert.deepEqual(r.serie_anos, [2022, 2024, 2026]);
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
/* 8b. Gráficos: a série real do IBGE e a geometria                    */
/* ================================================================== */

/* A série de São Paulo como o servidor a devolve, medida em
 * /api/consulta?fonte=ibge&nivel=uf&uf=35&ano=2024: 22 pontos, e 2007, 2010,
 * 2022 e 2023 não existem. O número sozinho erra o eixo — o que dá sentido ao
 * `serie_anos` que vem ao lado. */
const SERIE_SP = {
    serie_anos: [2001, 2002, 2003, 2004, 2005, 2006, 2008, 2009,
        2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020, 2021,
        2024, 2025, 2026],
    serie: [37630106, 38177742, 38709320, 39825226, 40442795, 41055734,
        41011638, 41384039, 41587182, 41901219, 43663669, 44035304,
        44396484, 44749699, 45094866, 45538936, 45919049, 46289333,
        46649132, 45973194, 46081801, 46179008],
};

/** Os registros de município que a consulta devolve: `valor: null` é ausente. */
const REGISTROS = [
    { cod: '3550308', nome: 'São Paulo', valor: 11895578 },
    { cod: '3304557', nome: 'Rio de Janeiro', valor: 6729894 },
    { cod: '5300108', nome: 'Brasília', valor: 2982818 },
    { cod: '1504752', nome: 'Mojuí dos Campos', valor: null },
    { cod: '2200954', nome: 'Aroeiras do Itaim', valor: null },
];

/** Conta nós de uma tag na árvore, para medir o que foi desenhado. */
function nosDe(el, tag) {
    const alvo = String(tag).toLowerCase();
    let n = 0;
    const visita = (no) => {
        for (const f of no.children || []) {
            if (String(f.tagName).toLowerCase() === alvo) n++;
            visita(f);
        }
    };
    visita(el);
    return n;
}

/** Todo atributo `d` desenhado na árvore. */
function caminhosDe(el) {
    const out = [];
    const visita = (no) => {
        for (const f of no.children || []) {
            if (String(f.tagName).toLowerCase() === 'path' && f.getAttribute) {
                out.push(String(f.getAttribute('d') || ''));
            }
            visita(f);
        }
    };
    visita(el);
    return out;
}

/** Percorre a árvore em profundidade, para medir os nós desenhados. */
function* percorrer(el) {
    for (const f of el.children || []) {
        yield f;
        yield* percorrer(f);
    }
}

/** ConTAINER vazio para os testes de desenho. */
function caixa(doc) {
    return doc.createElement('div');
}

/* --- a lacuna interrompe a linha ------------------------------------ */

teste('a série real do IBGE vira quatro segmentos, um por bloco de anos', () => {
    const s = segmentosLinha(paresDaSerie(SERIE_SP));
    assert.equal(s.length, 4,
        `2001-2006, 2008-2009, 2011-2021 e 2024-2026; veio ${s.length}`);
    assert.deepEqual(s.map((seg) => seg.length), [6, 2, 11, 3]);
    assert.equal(s[0][0].r, 2001);
    assert.equal(s[3][0].r, 2024, 'o quarto segmento começa em 2024');
});

teste('a série real não tem nenhum ano de lacuna desenhado', () => {
    const anos = segmentosLinha(paresDaSerie(SERIE_SP)).flat().map((p) => p.r);
    for (const fantasma of [2007, 2010, 2022, 2023]) {
        assert.ok(!anos.includes(fantasma),
            `${fantasma} não foi publicado e não pode virar ponto`);
    }
    assert.equal(anos.length, 22, 'os 22 pontos publicados estão todos');
});

teste('série inteira desenha em um segmento só', () => {
    const inteiro = [];
    for (let r = 2000; r <= 2020; r++) inteiro.push({ r, v: 1000 + r });
    const s = segmentosLinha(inteiro);
    assert.equal(s.length, 1, '21 anos seguidos são uma linha só');
    assert.equal(s[0].length, 21);
});

teste('valor ausente no meio não conecta os pontos por cima', () => {
    /* 2021 não tem valor: ligar 2020 a 2022 desenharia uma rampa que
     * affirmaria um dado que não existe. */
    const s = segmentosLinha([
        { r: 2020, v: 5 }, { r: 2021, v: null }, { r: 2022, v: 7 },
    ]);
    assert.equal(s.length, 2, `2020 e 2022 não podem virar um segmento só; veio ${s.length}`);
    assert.deepEqual(s.map((seg) => seg.map((p) => p.r)), [[2020], [2022]]);
});

teste('série vazia e série de um ponto não quebram', () => {
    assert.deepEqual(segmentosLinha([]), []);
    assert.deepEqual(segmentosLinha(null), []);
    assert.deepEqual(segmentosLinha(undefined), []);
    assert.deepEqual(segmentosLinha([{ r: 2024, v: 10 }]),
        [[{ r: 2024, v: 10 }]], 'um ponto isolado é um segmento de um ponto');
    assert.deepEqual(segmentosLinha([{ r: 2024, v: null }]), [],
        'e um ponto sem valor não é segmento nenhum');
});

/* --- a resposta real é lida com o ano ao lado ----------------------- */

teste('paresDaSerie casa cada valor com o ano dele', () => {
    const p = paresDaSerie(SERIE_SP);
    assert.equal(p.length, 22);
    assert.deepEqual(p[0], { r: 2001, v: 37630106 });
    assert.deepEqual(p[19], { r: 2024, v: 45973194 });
    assert.deepEqual(p.map((x) => x.r), SERIE_SP.serie_anos);
});

teste('paresDaSerie sem os anos não inventa o eixo', () => {
    /* Série sem `serie_anos` é o formato antigo. Sem o ano, cada valor vira
     * posição e o gráfico mentiria sobre a lacuna — então não há gráfico. */
    assert.deepEqual(paresDaSerie({ serie: [1, 2, 3] }), []);
    assert.deepEqual(paresDaSerie({ serie_anos: [2022, 2024] }), []);
    assert.deepEqual(paresDaSerie(null), []);
    assert.deepEqual(paresDaSerie({}), []);
});

teste('paresDaSerie guarda a lacuna explícita do servidor', () => {
    const p = paresDaSerie({ serie: [1, null, 3], serie_anos: [2001, 2002, 2003] });
    assert.equal(p.length, 3, 'o ano 2002 existe; o valor é que não');
    assert.deepEqual(p[1], { r: 2002, v: null });
    assert.equal(segmentosLinha(p).length, 2, 'e ele corta a linha em dois');
});

/* --- geometria: números, nunca NaN ---------------------------------- */

teste('a geometria da linha real tem 4 polilinhas e nenhum NaN', () => {
    const g = geometriaLinha(paresDaSerie(SERIE_SP), { largura: 640, altura: 220 });
    assert.equal(g.segmentos.length, 4, `veio ${g.segmentos.length}`);
    for (const seg of g.segmentos) {
        assert.ok(seg.pontos.length >= 1);
        for (const p of seg.pontos) {
            assert.ok(Number.isFinite(p.x), `x finito; veio ${p.x}`);
            assert.ok(Number.isFinite(p.y), `y finito; veio ${p.y}`);
        }
        for (let i = 1; i < seg.pontos.length; i++) {
            assert.ok(seg.pontos[i].x > seg.pontos[i - 1].x,
                'dentro de um segmento o ano cresce para a direita');
        }
    }
});

teste('a lacuna é distância no eixo, não uma linha atravessada', () => {
    /* 2009 → 2011 são dois anos, e na escala de 2001-2026 isso é uma distância
     * real. Se o gráfico "ligasse" os pontos, o x do fim do 2º segmento seria
     * o x do começo do 3º. */
    const g = geometriaLinha(paresDaSerie(SERIE_SP), { largura: 640, altura: 220 });
    const fimSegundo = g.segmentos[1].pontos[g.segmentos[1].pontos.length - 1].x;
    const inicioTerceiro = g.segmentos[2].pontos[0].x;
    assert.ok(inicioTerceiro - fimSegundo > 0.05 * 640,
        `há um vão visível entre 2009 e 2011; veio ${inicioTerceiro - fimSegundo}`);
    /* E o vão de 2021 → 2024 (três anos, dois deles sem publicação) é três
     * vezes o vão de 2001 → 2002 (um ano). */
    const vaoLongo = g.segmentos[3].pontos[0].x - g.segmentos[2].pontos[g.segmentos[2].pontos.length - 1].x;
    const vaoCurto = g.segmentos[0].pontos[1].x - g.segmentos[0].pontos[0].x;
    assert.ok(vaoLongo > vaoCurto * 2.5,
        `2011→2024 é muito maior que 2001→2002; ${vaoLongo} vs ${vaoCurto}`);
});

teste('a geometria da linha sem pontos é vazia, não quebrada', () => {
    const g = geometriaLinha([], {});
    assert.deepEqual(g.segmentos, []);
    assert.equal(g.vazio, true);
});

teste('barras com todos os valores iguais não divide por zero', () => {
    const g = geometriaBarras([
        { nome: 'A', valor: 5 }, { nome: 'B', valor: 5 }, { nome: 'C', valor: 5 },
    ], { largura: 600, altura: 200 });
    assert.equal(g.barras.length, 3);
    for (const b of g.barras) {
        assert.ok(Number.isFinite(b.h), `altura finita; veio ${b.h}`);
        assert.ok(b.h > 0, 'valor 5 é barra cheia a partir de zero, não altura zero');
    }
});

teste('barras com valor ausente não viram barra de altura zero', () => {
    /* Uma barra de altura 0 e uma barra de valor 0 são a mesma coisa na tela:
     * as duas diriam "zero". O ausente tem que aparecer como ausência. */
    const g = geometriaBarras(REGISTROS, { largura: 600, altura: 200 });
    const saoPaulo = g.barras.find((b) => b.rotulo === 'São Paulo');
    const lacuna = g.barras.find((b) => b.rotulo === 'Mojuí dos Campos');
    assert.ok(saoPaulo && saoPaulo.ausente === false);
    assert.equal(saoPaulo.h > 0, true);
    assert.equal(lacuna.ausente, true, 'o município sem dado é uma lacuna');
    assert.equal(lacuna.h, undefined, 'e não tem altura — zero é outro dado');
    assert.equal(g.dominio.max, 11895578,
        `o ausente não entra no domínio; veio ${g.dominio.max}`);
});

teste('barras de valor zero têm altura zero, e não altura qualquer', () => {
    const g = geometriaBarras([{ nome: 'A', valor: 0 }, { nome: 'B', valor: 0 }], {});
    for (const b of g.barras) {
        assert.equal(b.h, 0, `zero é zero; veio ${b.h}`);
        assert.ok(Number.isFinite(b.h));
    }
});

teste('barras ordenam por valor e mantêm a ordem da resposta', () => {
    const g = geometriaBarras(REGISTROS, { maximo: 3 });
    assert.deepEqual(g.barras.map((b) => b.rotulo), ['São Paulo', 'Rio de Janeiro', 'Brasília']);
    const nulos = geometriaBarras(REGISTROS, { maximo: 99 });
    assert.equal(nulos.barras.length, 5, 'com maximo 99 entra tudo, lacuna incluída');
    assert.deepEqual(nulos.barras.map((b) => b.rotulo),
        ['São Paulo', 'Rio de Janeiro', 'Brasília', 'Mojuí dos Campos', 'Aroeiras do Itaim']);
});

teste('donut com lista vazia não gera NaN nem arco fantasma', () => {
    assert.deepEqual(fatiasDonut([]), []);
    const g = geometriaDonut([], { tamanho: 160 });
    assert.deepEqual(g.arcos, [], 'sem fatia não há path para desenhar');
    assert.ok(Number.isFinite(g.anel.raio));
    assert.ok(Number.isFinite(g.anel.cx));
});

teste('donut com fatia de tamanho zero não desenha arco', () => {
    /* 2024 tem dado dos 5570 municípios: a fatia "sem dado" é 0%. Desenhar
     * um arco de 0° com a flag de arco grande ambígua produz um círculo
     * inteiro, que afirmaria que existe gente sem dado. */
    const fatias = publicacaoDoAno(5570, 5570);
    assert.equal(fatias.length, 2, 'as duas fatias existem');
    assert.equal(fatias[1].valor, 0);
    const g = geometriaDonut(fatiasDonut(fatias), { tamanho: 160 });
    assert.equal(g.arcos.length, 1, `só a fatia com valor vira arco; veio ${g.arcos.length}`);
    assert.ok(!/NaN/.test(g.arcos[0].d), 'o d do arco não tem NaN');
});

teste('donut de lista toda zero não desenha nada', () => {
    assert.deepEqual(fatiasDonut([{ rotulo: 'A', valor: 0 }]), []);
    assert.deepEqual(fatiasDonut([{ rotulo: 'A', valor: -1 }]), []);
    assert.deepEqual(geometriaDonut(fatiasDonut([{ rotulo: 'A', valor: 0 }]), {}).arcos, []);
});

teste('publicacaoDoAno separa com dado de sem dado, e nunca de zero', () => {
    /* Medido em /api/consulta?fonte=ibge&ano=2001: com_dado 5560 de 5570. */
    const f = publicacaoDoAno(5570, 5560);
    assert.deepEqual(f.map((x) => x.valor), [5560, 10]);
    assert.equal(f[0].rotulo, 'Municípios com dado publicado');
    assert.equal(f[1].rotulo, 'Municípios sem dado publicado');
    assert.deepEqual(publicacaoDoAno(0, 0), [], 'sem resultado não há donut');
    assert.deepEqual(publicacaoDoAno(null, null), []);
    /* `com_dado` ausente não pode virar "todos têm dado": vira o oposto, e o
     * painel inteiro ficaria verde sobre um número que ninguém mediu. */
    const semKpi = publicacaoDoAno(5570, null);
    assert.deepEqual(semKpi.map((x) => x.valor), [0, 5570]);
});

teste('fatiasDonut soma 360 graus e ignora valor ausente', () => {
    const f = fatiasDonut([{ rotulo: 'a', valor: 3 }, { rotulo: 'b', valor: null }, { rotulo: 'c', valor: 1 }]);
    assert.equal(f.length, 3, 'a fatia ausente fica, com valor 0');
    assert.equal(f[1].valor, 0);
    const soma = f.reduce((s, x) => s + x.tamanho, 0);
    assert.ok(Math.abs(soma - 360) < 1e-9, `soma 360; veio ${soma}`);
});

teste('os rótulos do eixo de anos são as fronteiras dos blocos', () => {
    /* O eixo tem que mostrar onde a série quebra, e não uma amostra de anos
     * quaisquer. Os anos candidatos são o começo e o fim de cada segmento;
     * o que aperta demais é descartado. */
    const segs = segmentosLinha(paresDaSerie(SERIE_SP));
    const px = escalaLinear(2001, 2026, 28, 612);
    const r = rotulosDoEixoX(segs, { px, maximo: 8 });
    assert.ok(r.length <= 8, `no máximo 8 rótulos; veio ${r.length}`);
    assert.ok(r.includes(2001), 'o primeiro ano é fronteira');
    assert.ok(r.includes(2021) && r.includes(2024),
        `as fronteiras do maior corte aparecem; veio ${JSON.stringify(r)}`);
    for (const fantasma of [2007, 2010, 2022, 2023]) {
        assert.ok(!r.includes(fantasma), `${fantasma} não é fronteira de bloco`);
    }
    /* Nenhum rótulo pode cair em cima do anterior. */
    for (let i = 1; i < r.length; i++) {
        assert.ok(px(r[i]) - px(r[i - 1]) >= 34,
            `${r[i - 1]} e ${r[i]} não se atropelam`);
    }
    assert.deepEqual(rotulosDoEixoX(
        [[{ r: 2001, v: 1 }, { r: 2002, v: 2 }]],
        { px: escalaLinear(2001, 2002, 28, 612), maximo: 8 }),
    [2001, 2002], 'com poucos anos, todos aparecem');
    assert.deepEqual(rotulosDoEixoX([], {}), []);
    assert.deepEqual(rotulosDoEixoX(null, {}), []);
});

teste('a geometria declara as três lacunas da série real', () => {
    const g = geometriaLinha(paresDaSerie(SERIE_SP), { largura: 640, altura: 220 });
    assert.deepEqual(g.lacunas.map((l) => [l.de, l.ate, l.faltam]),
        [[2006, 2008, 1], [2009, 2011, 1], [2021, 2024, 2]],
        `os três cortes da série de São Paulo; veio ${JSON.stringify(g.lacunas)}`);
    for (const l of g.lacunas) {
        assert.ok(Number.isFinite(l.x) && l.x > 0 && l.x < 640);
    }
    assert.ok(g.lacunas[2].faltam === 2, '2022 e 2023 faltam juntos, e o desenho diz as duas');
});

teste('abreviaNumero encurta para o eixo e nunca vira 0', () => {
    assert.equal(abreviaNumero(45973194), '46 mi');
    assert.equal(abreviaNumero(null), '--');
    assert.equal(abreviaNumero(-1), '--', '-1 é ausente, não número');
    assert.notEqual(abreviaNumero(0), '0 mi');
});

teste('encurtaTexto corta sem quebrar o rótulo ao meio sem sentido', () => {
    assert.equal(encurtaTexto('Aroeiras do Itaim', 10), 'Aroeiras…');
    assert.equal(encurtaTexto('SP', 10), 'SP');
    assert.equal(encurtaTexto(null, 10), '');
});

teste('a paleta devolve cores distintas e uma cor de ausência distinta', () => {
    assert.deepEqual(coresCategoricas(0), []);
    const c = coresCategoricas(4);
    assert.equal(c.length, 4);
    assert.equal(new Set(c).size, 4, 'quatro fatias, quatro cores');
    for (const cor of c) {
        assert.match(cor, /^#[0-9a-f]{6}$/, `cor em hex; veio "${cor}"`);
    }
    assert.ok(!coresCategoricas(9).includes(COR_AUSENCIA),
        'a cor de ausência não pode ser a de uma fatia com valor');
    assert.match(corSequencial(0), /^#[0-9a-f]{6}$/);
    assert.notEqual(corSequencial(0), corSequencial(1));
    assert.equal(corSequencial(-1), corSequencial(0), 'fora da faixa, o extremo');
    assert.equal(corSequencial(2), corSequencial(1));
});

/* --- camada de desenho, com o DOM falso ---------------------------- */

teste('linha desenha um path por segmento: 4, na série real', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    linha(el, paresDaSerie(SERIE_SP), { doc, titulo: 'População de São Paulo' });
    assert.equal(nosDe(el, 'path'), 4,
        `quatro polilinhas — a lacuna não pode virar uma só; veio ${nosDe(el, 'path')}`);
    for (const d of caminhosDe(el)) {
        assert.ok(!/NaN|Infinity|undefined/.test(d), `o caminho é numérico; veio "${d}"`);
    }
    assert.equal(nosDe(el, 'line'), 3 + 5,
        'três marcações de lacuna e cinco linhas de grade');
});

teste('linha sem pontos não desenha svg nenhum', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    linha(el, [], { doc });
    assert.equal(nosDe(el, 'svg'), 0, 'série vazia não vira svg');
    assert.ok(el.textContent.includes('Sem série'),
        `e diz o que houve; veio "${el.textContent}"`);
});

teste('linha com valor ausente no meio não desenha reta por cima', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    linha(el, [{ r: 2020, v: 5 }, { r: 2021, v: null }, { r: 2022, v: 7 }], { doc });
    /* As duas pontas são pontos isolados: viram marcador, e não viram um
     * caminho. Um caminho aqui seria a rampa que inventa o dado de 2021. */
    assert.equal(caminhosDe(el).length, 0,
        `nenhum caminho atravessa o ausente; veio ${JSON.stringify(caminhosDe(el))}`);
    assert.equal(nosDe(el, 'circle'), 2, 'e as duas pontas aparecem como marcador');
});

teste('o eixo de anos da linha rotula as fronteiras e nenhum ano fantasma', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    linha(el, paresDaSerie(SERIE_SP), { doc });
    const anos = el.querySelectorAll('.eixo-ano').map((n) => n.textContent.trim());
    assert.ok(anos.length >= 4, `há rótulos de ano; veio ${JSON.stringify(anos)}`);
    for (const fantasma of ['2007', '2010', '2022', '2023']) {
        assert.ok(!anos.includes(fantasma),
            `${fantasma} não foi publicado e não vira rótulo de eixo; veio ${JSON.stringify(anos)}`);
    }
    assert.ok(anos.includes('2001') && anos.includes('2024') && anos.includes('2026'),
        `as fronteiras dos blocos aparecem; veio ${JSON.stringify(anos)}`);
});

teste('a marcação de lacuna nomeia os anos que faltam', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    linha(el, paresDaSerie(SERIE_SP), { doc });
    const marcas = [...percorrer(el)].filter((n) => n.getAttribute
        && n.getAttribute('data-lacuna'));
    assert.deepEqual(marcas.map((n) => n.getAttribute('data-lacuna')),
        ['2006-2008', '2009-2011', '2021-2024']);
    const texto = marcas[2].textContent;
    assert.ok(texto.includes('2022') && texto.includes('2023') && texto.includes('sem dado'),
        `o vão de dois anos diz quais são; veio "${texto}"`);
});

teste('o svg da linha é reprodutível e acessível', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    linha(el, paresDaSerie(SERIE_SP), { doc, titulo: 'População de São Paulo' });
    const svgEl = el.children[0];
    assert.equal(svgEl.tagName, 'SVG');
    assert.equal(svgEl.getAttribute('role'), 'img');
    assert.equal(svgEl.getAttribute('aria-label'), 'População de São Paulo');
    assert.equal(svgEl.getAttribute('viewBox'), '0 0 640 220');
});

teste('desenharLinha com doc explícito é o mesmo desenho', () => {
    /* As duas portas existem: a do contrato do brief, que tira o documento do
     * global, e a explícita, que o recebe. Precisam concordar. */
    const doc = criaDoc();
    const a = caixa(doc);
    const b = caixa(doc);
    desenharLinha(doc, a, paresDaSerie(SERIE_SP), { titulo: 'T' });
    linha(b, paresDaSerie(SERIE_SP), { doc, titulo: 'T' });
    assert.deepEqual(caminhosDe(a), caminhosDe(b));
    assert.equal(nosDe(a, 'path'), 4);
});

teste('barras desenha sem NaN mesmo com valores iguais', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    barras(el, [{ nome: 'A', valor: 5 }, { nome: 'B', valor: 5 }], { doc, titulo: 'Teste' });
    assert.equal(nosDe(el, 'rect'), 2);
    for (const r of [...percorrer(el)].filter((n) => String(n.tagName).toLowerCase() === 'rect')) {
        for (const k of ['x', 'y', 'width', 'height']) {
            assert.ok(Number.isFinite(Number(r.getAttribute(k))),
                `${k} finito no rect; veio ${r.getAttribute(k)}`);
        }
    }
});

teste('barras marcam a lacuna sem desenhar rect de altura zero', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    barras(el, REGISTROS, { doc, titulo: 'Municípios por valor' });
    const lacunas = [...percorrer(el)].filter((n) => n.getAttribute
        && n.getAttribute('data-ausente') === 'true');
    assert.equal(lacunas.length, 2, `duas lacunas marcadas; veio ${lacunas.length}`);
    const nulos = [...percorrer(el)].filter((n) => String(n.tagName).toLowerCase() === 'rect'
        && Number(n.getAttribute('height')) === 0);
    assert.equal(nulos.length, 0, 'nenhum rect de altura zero: seria lido como valor zero');
    assert.equal(nosDe(el, 'rect'), 3, 'só os três municípios com valor têm barra');
});

teste('donut com lista vazia não escreve nenhum caminho', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    donut(el, [], { doc, titulo: 'Publicação do ano' });
    assert.equal(caminhosDe(el).length, 0, 'sem fatia não há path');
    assert.ok(el.textContent.includes('Sem dado'),
        `a ausência de fatia é dita em português; veio "${el.textContent}"`);
});

teste('donut com fatias desenha arcos sem NaN', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    donut(el, fatiasDonut(publicacaoDoAno(5570, 5560)), { doc, titulo: 'Publicação do ano' });
    const d = caminhosDe(el);
    assert.equal(d.length, 2, `duas fatias, dois arcos; veio ${d.length}`);
    for (const x of d) assert.ok(!/NaN/.test(x), `arco numérico; veio "${x}"`);
    assert.ok(el.textContent.includes('5.560'), `a legenda mostra o número; veio "${el.textContent}"`);
    assert.ok(el.textContent.includes('99,8%'),
        `e a participação; veio "${el.textContent}"`);
});

teste('donut com uma fatia só desenha o anel inteiro em dois arcos', () => {
    /* 100% é um arco de 360°: início e fim no mesmo ponto. Um `A` só não tem
     * trajeto, e o anel sairia vazio. */
    const doc = criaDoc();
    const el = caixa(doc);
    donut(el, fatiasDonut([{ rotulo: 'Todos', valor: 5570 }]), { doc });
    const d = caminhosDe(el);
    assert.equal(d.length, 1, 'uma fatia, um caminho');
    assert.equal((d[0].match(/A/g) || []).length, 2,
        `o anel inteiro sai como dois arcos de 180°; veio "${d[0]}"`);
    assert.ok(!/NaN/.test(d[0]));
});

teste('kpi escreve "--" para valor ausente e nunca 0', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    kpi(el, { rotulo: 'Mínimo', valor: null }, { doc });
    assert.ok(el.textContent.includes('Mínimo'));
    assert.ok(el.textContent.includes('--'), `"--" no lugar do ausente; veio "${el.textContent}"`);
    assert.ok(!/(^|\s|\D)0(\s|$|\D)/.test(el.textContent),
        `zero nunca no lugar do ausente; veio "${el.textContent}"`);
});

teste('kpi formata o valor em português e mostra a unidade', () => {
    const doc = criaDoc();
    const el = caixa(doc);
    kpi(el, { rotulo: 'Total de municípios', valor: 5570, unidade: 'municípios' }, { doc });
    assert.ok(el.textContent.includes('5.570'),
        `separador de milhar pt-BR; veio "${el.textContent}"`);
    assert.ok(el.textContent.includes('municípios'));
});

teste('kpi cria o cartão quando não recebe um', () => {
    const doc = criaDoc();
    const cartao = kpi(null, { rotulo: 'Máximo', valor: 11895578 }, { doc });
    assert.equal(cartao.className, 'kpi');
    assert.equal(cartao.querySelector('.kpi-rotulo').textContent, 'Máximo');
    assert.ok(cartao.querySelector('.kpi-valor').textContent.includes('11.895.578'));
});

/* --- o app liga os gráficos, e só depois do clique ------------------ */

teste('nada é desenhado antes do clique (spec §10)', () => {
    const { doc } = appDeTeste({});
    /* O DOM falso não lê o index.html, então o aviso do estado vazio é
     * reproduzido aqui à mão. Que ele exista no arquivo servido é o que
     * `tests/test_web.sh` verifica, com grep no que o servidor entrega. */
    doc.porId.graficos.textContent = 'Ajuste os filtros e clique em Filtrar';
    doc.porId.kpis.textContent = '--';
    assert.equal(nosDe(doc.porId.graficos, 'svg'), 0, 'nenhum svg antes de Filtrar');
    assert.equal(nosDe(doc.porId.graficos, 'rect'), 0, 'nenhuma barra antes de Filtrar');
    assert.equal(nosDe(doc.porId.graficos, 'circle'), 0, 'nenhum ponto antes de Filtrar');
    assert.ok(doc.porId.graficos.textContent.includes('Ajuste os filtros e clique em Filtrar'),
        `o aviso do HTML continua na tela; veio "${doc.porId.graficos.textContent}"`);
    assert.equal(doc.porId.kpis.textContent, '--', 'e os KPIs continuam em "--"');
});

await testeAsync('depois de Filtrar o painel de gráficos troca o aviso por svg', async () => {
    const { doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: ['SP'] } },
        '/api/consulta': {
            json: {
                total: 5570,
                kpis: { min: 854, max: 11895578, media: 38165, soma: 212583750, com_dado: 5570 },
                registros: REGISTROS,
            },
        },
    });
    await doc.porId.filtrar.click();
    const painel = doc.porId.graficos;
    assert.ok(nosDe(painel, 'svg') >= 2, `barras e donut desenhados; veio ${nosDe(painel, 'svg')}`);
    assert.ok(!painel.textContent.includes('Ajuste os filtros'),
        'o aviso de estado vazio sai depois da consulta');
    assert.ok(painel.textContent.includes('São Paulo'), 'o dado está no painel');
});

await testeAsync('a série do IBGE vira 4 caminhos no DOM depois do clique', async () => {
    const { doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: ['35'] } },
        '/api/consulta': {
            json: {
                total: 1,
                kpis: { min: 45973194, max: 45973194, media: 45973194, soma: 45973194, com_dado: 1 },
                registros: [{ cod: '35', nome: 'SP', valor: 45973194 }],
                serie: SERIE_SP.serie,
                serie_anos: SERIE_SP.serie_anos,
            },
        },
    });
    await doc.porId.filtrar.click();
    const painel = doc.porId.graficos;
    const linhas = [...percorrer(painel)].filter((n) => String(n.tagName).toLowerCase() === 'path'
        && /L/.test(String(n.getAttribute('d') || '')));
    assert.equal(linhas.length, 4,
        `a linha do gráfico tem 4 segmentos, um por bloco de anos; veio ${linhas.length}`);
    const anos = painel.querySelectorAll('.eixo-ano').map((n) => n.textContent.trim());
    for (const fantasma of ['2007', '2010', '2022', '2023']) {
        assert.ok(!anos.includes(fantasma),
            `${fantasma} não foi publicado e não vira rótulo; veio ${JSON.stringify(anos)}`);
    }
    assert.ok(anos.includes('2021') && anos.includes('2024'),
        `as fronteiras dos cortes viram rótulo; veio ${JSON.stringify(anos)}`);
    const marcas = [...percorrer(painel)].filter((n) => n.getAttribute && n.getAttribute('data-lacuna'));
    assert.equal(marcas.length, 3, 'e os três cortes são desenhados, não só omitidos');
});

await testeAsync('consulta sem série não desenha a linha', async () => {
    const { doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: [] } },
        '/api/consulta': {
            json: {
                total: 5,
                kpis: { min: 854, max: 11895578, media: 38165, soma: 212583750, com_dado: 5570 },
                registros: REGISTROS,
            },
        },
    });
    await doc.porId.filtrar.click();
    const painel = doc.porId.graficos;
    /* Só barra e donut; nenhum caminho de linha. */
    const linhas = [...percorrer(painel)].filter((n) => String(n.tagName).toLowerCase() === 'path'
        && /L/.test(String(n.getAttribute('d') || '')));
    assert.equal(linhas.length, 0, `sem série não há linha; veio ${linhas.length}`);
    assert.ok(painel.textContent.includes('escolha uma UF'),
        `e o painel diz o que fazer para a série aparecer; veio "${painel.textContent.slice(0, 200)}"`);
});

await testeAsync('a linha mostra a série inteira, não só o ano escolhido', async () => {
    /* O ano do seletor é o recorte da barra e do donut; a linha é a série
     * histórica inteira, porque é a lacuna dela que a spec §15 manda mostrar. */
    const { app, doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: ['35'] } },
        '/api/consulta': {
            json: {
                total: 1,
                kpis: { min: 45973194, max: 45973194, media: 45973194, soma: 45973194, com_dado: 1 },
                registros: [{ cod: '35', nome: 'SP', valor: 45973194 }],
                serie: SERIE_SP.serie,
                serie_anos: SERIE_SP.serie_anos,
            },
        },
    });
    await doc.porId.filtrar.click();
    const e = app.estado();
    assert.equal(e.seriePontos.length, 22, 'os 22 pontos casados com seus anos');
    assert.deepEqual(e.seriePontos[19], { r: 2024, v: 45973194 });
});

await testeAsync('o donut mostra quem não tem dado, e nunca um zero inventado', async () => {
    /* Medido: ano 2001 devolve com_dado 5560 de 5570. */
    const { doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2001, 2024], ufs: [] } },
        '/api/consulta': {
            json: {
                total: 5570,
                kpis: { min: 800, max: 10499133, media: 31004, soma: 172385826, com_dado: 5560 },
                registros: REGISTROS,
            },
        },
    }, { ano: '2001' });
    await doc.porId.filtrar.click();
    const texto = doc.porId.graficos.textContent;
    assert.ok(texto.includes('5.560'), `a fatia com dado mostra o número; veio "${texto}"`);
    assert.ok(/sem dado publicado: 10/.test(texto),
        `e a fatia sem dado também — 10 municípios de 2001; veio "${texto}"`);
});

await testeAsync('sem com_dado o donut não diz que todos têm dado', async () => {
    const { doc } = appDeTeste({
        '/api/opcoes': { json: { anos: [2024], ufs: [] } },
        '/api/consulta': {
            json: { total: 5570, kpis: { min: 854, max: 11895578, media: 38165 }, registros: REGISTROS },
        },
    });
    await doc.porId.filtrar.click();
    const texto = doc.porId.graficos.textContent;
    assert.ok(/sem dado publicado: 5\.570/.test(texto),
        `com_dado ausente vira "sem dado" no total, não 5.570 com dado; veio "${texto}"`);
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

/* ------------------------------------------------------------------ */
/* campo vazio é ausência, e não zero                                  */
/* ------------------------------------------------------------------ */

/* Achado I3 da revisão da Tarefa 10. A spec §4 e o foco de revisão 4 dizem que
 * campo vazio e ".." são "sem dado" e nunca viram 0. Antes disto,
 * `valorPresente('')` devolvia true, porque `Number('') === 0`, e o campo
 * vazio de um CSV do IBGE entrava na tela como um zero medido. */
teste('campo vazio do CSV é ausência, e não zero', () => {
    assert.equal(valorPresente(''), false, '"" não é valor');
    assert.equal(valorPresente('   '), false, '"   " também não é valor');
    assert.equal(valorPresente('..'), false, '".." é o marcador do IBGE');
});

teste('zero de verdade continua sendo valor', () => {
    /* O outro extremo do mesmo erro: tratar 0 como ausente apagaria um
     * município que legitimamente tem zero. */
    assert.equal(valorPresente(0), true, 'o número 0 existe');
    assert.equal(valorPresente('0'), true, 'a string "0" existe');
    assert.equal(valorPresente(-1), false, 'a sentinela do aggregate é ausência');
});

teste('uma barra com campo vazio não vira barra de altura zero', () => {
    const g = geometriaBarras([{ nome: 'A', valor: '' }, { nome: 'B', valor: 5 }], {});
    const a = g.barras.find((b) => b.rotulo === 'A');
    assert.ok(a, 'a barra de A existe no desenho');
    assert.equal(a.ausente, true, 'e vem marcada como ausente');
    assert.notEqual(a.valor, 0, 'e não carrega o zero que o campo vazio produzia');
});

/* ------------------------------------------------------------------ */
/* concordância                                                         */
/* ------------------------------------------------------------------ */

/* Achado M2 da revisão: "1 estados no resultado", "1 localidades no resultado"
 * e o KPI "1 municípios". Três textos, o mesmo defeito. */
teste('o substantivo concorda com o número', () => {
    assert.equal(plural(1, 'município', 'municípios'), 'município');
    assert.equal(plural(2, 'município', 'municípios'), 'municípios');
    assert.equal(plural(0, 'município', 'municípios'), 'municípios', 'zero é plural');
    assert.equal(plural(1, 'estado', 'estados'), 'estado');
    assert.equal(plural(27, 'estado', 'estados'), 'estados');
});

testeAsync('o cartão do mapa concorda: um estado é "1 estado no resultado"', async () => {
    const doc = docDeTeste({ nivel: 'uf', uf: '35' });
    const resposta = {
        total: 1,
        kpis: { com_dado: 1 },
        registros: [{ cod: '35', nome: 'São Paulo', valor: 1 }],
    };
    const app = criarApp({ doc, fetch: fetchFalso({ consulta: resposta }) });
    await app.filtrar();
    const texto = doc.porId.mapa.textContent || '';
    assert.ok(texto.includes('1 estado no resultado'),
        `o mapa escreve "1 estado no resultado"; veio "${texto}"`);
    assert.ok(!/1 estados/.test(texto), `e nunca "1 estados"; veio "${texto}"`);
});

testeAsync('o rótulo do KPI diz "Total de estados" quando o recorte é por UF', async () => {
    const doc = docDeTeste({ nivel: 'uf', uf: '35' });
    const resposta = {
        total: 1,
        kpis: { com_dado: 1 },
        registros: [{ cod: '35', nome: 'São Paulo', valor: 1 }],
    };
    const app = criarApp({ doc, fetch: fetchFalso({ consulta: resposta }) });
    await app.filtrar();
    const rotulos = doc.porId.kpis.querySelectorAll('.kpi-rotulo').map((e) => e.textContent);
    assert.ok(rotulos.some((r) => r === 'Total de estados'),
        `o primeiro KPI é "Total de estados"; veio ${JSON.stringify(rotulos)}`);
    assert.ok(!rotulos.some((r) => r === 'Total de municípios'),
        'e não "Total de municípios", que mentiria sobre o recorte');
});

/* ------------------------------------------------------------------ */
/* o app liga o mapa (Tarefa 11)                                       */
/* ------------------------------------------------------------------ */

/* O `map.js` tem 21 testes próprios, e nenhum deles diz que o APP o chama.
 * Um módulo perfeito e desconectado passa a suíte inteira e não pinta nada
 * na tela — a mesma classe de buraco que aprovou `ufs: []` na Tarefa 7. Por
 * isso estes testes exercitam o caminho app → fetch da malha → desenho. */

const MALHA_DOIS = {
    type: 'FeatureCollection',
    features: [
        {
            type: 'Feature',
            properties: { codarea: '1100015', centroide: [-62075, -12500] },
            geometry: {
                type: 'Polygon',
                coordinates: [[[-63000, -13000], [-61000, -13000], [-61000, -12000],
                    [-63000, -12000], [-63000, -13000]]],
            },
        },
        {
            type: 'Feature',
            properties: { codarea: '3550308', centroide: [-46500, -23500] },
            geometry: {
                type: 'Polygon',
                coordinates: [[[-47000, -24000], [-46000, -24000], [-46000, -23000],
                    [-47000, -23000], [-47000, -24000]]],
            },
        },
    ],
};

/** Percorre a árvore do DOM falso devolvendo os elementos de uma tag. */
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

testeAsync('carregarMapa pinta o mapa em cinza, sem buscar nenhuma consulta', async () => {
    const { app, doc, fetch } = appDeTeste({ '/api/geo/municipios': { json: MALHA_DOIS } });

    await app.carregarMapa();

    const paths = tagsDe(doc.porId.mapa, 'path');
    assert.equal(paths.length, 2, `a malha foi desenhada; vieram ${paths.length} <path>`);
    for (const p of paths) {
        assert.equal(p.getAttribute('class'), 'mapa-sem-dado',
            'sem consulta, todo município é ausência');
    }
    assert.ok(!fetch.urls().some((u) => u.includes('consulta')),
        `abrir não busca consulta; veio ${JSON.stringify(fetch.urls())}`);
});

testeAsync('depois de Filtrar, o município com dado sai da cor de ausência', async () => {
    const resposta = {
        total: 2,
        kpis: { com_dado: 1 },
        registros: [
            { cod: '1100015', nome: 'Porto Velho', valor: 5 },
            { cod: '3550308', nome: 'São Paulo', valor: -1 },
        ],
    };
    const { app, doc } = appDeTeste({
        '/api/geo/municipios': { json: MALHA_DOIS },
        '/api/consulta': { json: resposta },
    });

    await app.carregarMapa();
    await app.filtrar();

    const paths = tagsDe(doc.porId.mapa, 'path');
    assert.equal(paths.length, 2, `o mapa continua desenhado; vieram ${paths.length}`);
    const comDado = paths.find((p) => p.getAttribute('data-codarea') === '1100015');
    const semDado = paths.find((p) => p.getAttribute('data-codarea') === '3550308');

    assert.ok(comDado && comDado.getAttribute('fill'),
        'o município com dado tem cor própria da escala');
    assert.equal(semDado.getAttribute('class'), 'mapa-sem-dado',
        'o município com valor -1 continua na cor de ausência');
});

testeAsync('clicar num município escreve nome e valor no painel', async () => {
    const resposta = {
        total: 1,
        kpis: { com_dado: 1 },
        registros: [{ cod: '3550308', nome: 'São Paulo', valor: 12345678 }],
    };
    const { app, doc } = appDeTeste({
        '/api/geo/municipios': { json: MALHA_DOIS },
        '/api/consulta': { json: resposta },
    });

    await app.carregarMapa();
    await app.filtrar();

    const sp = tagsDe(doc.porId.mapa, 'path')
        .find((p) => p.getAttribute('data-codarea') === '3550308');
    assert.ok(sp, 'o <path> de São Paulo existe');
    await sp.click();

    const detalhe = doc.porId.mapa.querySelector('.mapa-detalhe');
    assert.ok(detalhe, 'o clique escreveu o painel de detalhe');
    assert.ok(detalhe.textContent.includes('São Paulo'),
        `com o nome acentuado; veio "${detalhe.textContent}"`);
    assert.ok(detalhe.textContent.includes('12.345.678'),
        `e com o valor formatado em pt-BR; veio "${detalhe.textContent}"`);
});

testeAsync('Filtrar sem esperar carregarMapa ainda pinta o mapa', async () => {
    /* A corrida: a ponte com o browser dispara `carregarMapa` sem esperar, e um
     * clique em Filtrar pode chegar antes de ela resolver. Sem a espera dentro
     * de `filtrar`, o primeiro mapa da sessão saía como o aviso "sem dados"
     * com a consulta completa desenhada ao lado. */
    const resposta = {
        total: 1,
        kpis: { com_dado: 1 },
        registros: [{ cod: '1100015', nome: 'Porto Velho', valor: 5 }],
    };
    const { app, doc } = appDeTeste({
        '/api/geo/municipios': { json: MALHA_DOIS },
        '/api/consulta': { json: resposta },
    });

    /* Sem chamar app.carregarMapa(): é o clique que tem que trazer a malha. */
    await app.filtrar();

    const paths = tagsDe(doc.porId.mapa, 'path');
    assert.equal(paths.length, 2, `o mapa foi pintado; vieram ${paths.length} <path>`);
    assert.ok(!/Sem dados/.test(doc.porId.mapa.textContent),
        `e não é o aviso de vazio; veio "${doc.porId.mapa.textContent}"`);
});

testeAsync('a segunda consulta não busca a malha de novo', async () => {
    const resposta = {
        total: 1,
        kpis: { com_dado: 1 },
        registros: [{ cod: '1100015', nome: 'Porto Velho', valor: 5 }],
    };
    const { app, fetch } = appDeTeste({
        '/api/geo/municipios': { json: MALHA_DOIS },
        '/api/consulta': { json: resposta },
    });

    await app.filtrar();
    await app.filtrar();

    const geos = fetch.urls().filter((u) => u.includes('/api/geo/'));
    assert.equal(geos.length, 1,
        `a malha é o desenho do mapa e fica memorizada; buscada ${geos.length} vezes`);
});

testeAsync('clicar num município sem dado diz "sem dado", e não 0', async () => {
    const resposta = {
        total: 1,
        kpis: { com_dado: 0 },
        registros: [{ cod: '1100015', nome: 'Porto Velho', valor: -1 }],
    };
    const { app, doc } = appDeTeste({
        '/api/geo/municipios': { json: MALHA_DOIS },
        '/api/consulta': { json: resposta },
    });

    await app.carregarMapa();
    await app.filtrar();
    const pv = tagsDe(doc.porId.mapa, 'path')
        .find((p) => p.getAttribute('data-codarea') === '1100015');
    await pv.click();

    const detalhe = doc.porId.mapa.querySelector('.mapa-detalhe');
    assert.ok(detalhe, 'o clique escreveu o detalhe');
    assert.ok(/sem dado/.test(detalhe.textContent),
        `diz "sem dado"; veio "${detalhe.textContent}"`);
    assert.ok(!/\b0\b/.test(detalhe.textContent),
        `e nunca 0 como valor; veio "${detalhe.textContent}"`);
});

/* ------------------------------------------------------------------ */
/* o eixo Y precisa caber na margem, e a legenda precisa concordar   */
/* ------------------------------------------------------------------ */

/* Achados C1 e C2 da revisão da Tarefa 10, ambos medidos no navegador e
 * nenhum pego pelos 118 testes que existiam.
 *
 * C1: a margem esquerda era fixa em 28px e o rótulo é ancorado em
 * `x = margem - 4` com `text-anchor: end`. "46,6 mi" tem ~38px, então saía
 * pela esquerda do viewBox — e o SVG recorta por padrão. A linha lia
 * "6 mi / 4 mi / 1 mi / 9 mi / 6 mi" em vez dos valores de verdade.
 *
 * C2: os arcos do anel saíam sem `cor` e a legenda fazia `a.cor || AUSENCIA`,
 * então toda fatia tinha o quadradinho cinza de "sem dado" com o arco verde. */

const PX_POR_CHAR_EIXO = 5.4;   /* o que .eixo-valor mede: monoespaçada 9px */

teste('a margem esquerda cabe o rótulo mais largo do eixo Y', () => {
    /* A série real de São Paulo: 37,6 a 46,6 milhões. */
    const g = geometriaLinha(paresDaSerie(SERIE_SP));
    const rotulos = quatroTicks(g.dominio.min, g.dominio.max).map((t) => abreviaNumero(t));
    const maisLargo = Math.max(...rotulos.map((r) => r.length * PX_POR_CHAR_EIXO));

    assert.ok(maisLargo > 24,
        `o rótulo de verdade é mais largo que a margem antiga: ${maisLargo.toFixed(1)}px`);
    assert.ok(g.margem >= maisLargo + 4,
        `a margem (${g.margem}) cabe o rótulo (${maisLargo.toFixed(1)}px) mais o deslocamento de 4`);
    assert.ok(g.margem * 2 < g.largura,
        `e não come o gráfico inteiro: margem ${g.margem} de largura ${g.largura}`);
});

teste('nenhum rótulo do eixo Y começa fora do viewBox, por construção', () => {
    for (const serie of [paresDaSerie(SERIE_SP), [{ r: 2024, v: 1 }], [{ r: 2024, v: 11895578 }]]) {
        const g = geometriaLinha(serie);
        for (const t of quatroTicks(g.dominio.min, g.dominio.max)) {
            const larguraRotulo = abreviaNumero(t).length * PX_POR_CHAR_EIXO;
            const xEsquerdo = g.margem - 4 - larguraRotulo;
            assert.ok(xEsquerdo >= 0,
                `"${abreviaNumero(t)}" começa em ${xEsquerdo.toFixed(1)} e sai do viewBox`);
        }
    }
});

teste('as barras também abrem espaço para o rótulo do eixo', () => {
    const g = geometriaBarras([{ nome: 'São Paulo', valor: 11895578 }], {});
    const rotulos = quatroTicks(0, 11895578).map((t) => abreviaNumero(t));
    const maisLargo = Math.max(...rotulos.map((r) => r.length * PX_POR_CHAR_EIXO));
    assert.ok(g.margem >= maisLargo + 4,
        `a margem das barras (${g.margem}) cabe "${maisLargo > 0 ? rotulos.join('" "') : ''}"`);
});

teste('barras todas zero põem o zero na base, e não no meio', () => {
    /* I5: com domínio 0..0 a escala degenerava para o meio da faixa, e o "0"
     * da grade ficava 72px acima das barras, que estavam com altura 0. */
    const g = geometriaBarras([{ nome: 'A', valor: 0 }, { nome: 'B', valor: 0 }], {});
    assert.equal(g.dominio.max, 0, 'o maior valor é zero mesmo');
    assert.ok(Math.abs(g.escalaY(0) - g.base) < 0.01,
        `o zero da escala fica na base das barras; base=${g.base}, escalaY(0)=${g.escalaY(0)}`);
    assert.deepEqual(g.ticks, [0], 'e a grade mostra só o zero, na base');
});

teste('a legenda do anel usa a cor de cada arco, e não a cor de ausência', () => {
    const fatias = fatiasDonut([
        { rotulo: 'com dado', valor: 4000 },
        { rotulo: 'sem dado', valor: 1570 },
    ]);
    const g = geometriaDonut(fatias, {});
    assert.ok(g.arcos.length >= 1, `há arcos; vieram ${g.arcos.length}`);

    for (let i = 0; i < g.arcos.length; i++) {
        assert.ok(g.arcos[i].cor, `o arco ${i} tem cor própria`);
        assert.notEqual(g.arcos[i].cor, COR_AUSENCIA,
            `a fatia ${i} não é a cor de ausência; veio "${g.arcos[i].cor}"`);
        assert.equal(g.arcos[i].cor, g.cores[i % g.cores.length],
            'e é exatamente a cor que o desenho pinta no arco — legenda e desenho não podem divergir');
    }
});

testeAsync('o quadradinho da legenda sai com a cor do arco', async () => {
    const doc = criaDoc();
    const el = doc.createElement('div');
    const fatias = fatiasDonut([
        { rotulo: 'com dado', valor: 4000 },
        { rotulo: 'sem dado', valor: 1570 },
    ]);
    donut(el, fatias, { doc });

    const marcas = [];
    const visita = (no) => {
        if (no.className === 'legenda-cor') marcas.push(no.getAttribute('data-cor'));
        for (const f of no.children || []) visita(f);
    };
    visita(el);

    assert.ok(marcas.length >= 2, `a lista tem as duas fatias; vieram ${marcas.length}`);
    for (const m of marcas) {
        assert.notEqual(m, COR_AUSENCIA,
            `nenhum quadradinho em cinza de ausência; veio "${m}"`);
    }
    assert.equal(new Set(marcas).size, marcas.length,
        `e cada fatia com a sua cor; vieram ${JSON.stringify(marcas)}`);
});

/* ------------------------------------------------------------------ */
/* o ano inicial é o mais recente, e não o mais antigo                 */
/* ------------------------------------------------------------------ */

/* I7: o comentário em filters.js dizia "usa o mais recente" e o código usava
 * `anos[0]` de uma lista crescente — ou seja, 2001. */
teste('o ano selecionado por padrão é o mais recente', () => {
    const doc = criaDoc();
    const select = doc.porId.ano;
    select.value = '';
    /* O que /api/opcoes devolve: 22 anos em ordem crescente. */
    const anos = [2001, 2002, 2003, 2004, 2005, 2006, 2008, 2009, 2011, 2012,
        2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020, 2021, 2024];
    preencherOpcoes(doc, { anos, ufs: [] }, 'todos', '');
    assert.equal(select.value, '2024',
        `com a lista em ordem crescente, o ano escolhido é o maior; veio "${select.value}"`);
});

/* ================================================================== */

if (falhas > 0) {
    console.error('FALHOU:');
    for (const f of falhasDetalhadas) console.error('  - ' + f);
    console.error(`\n${falhas} de ${testes} testes do front falharam`);
    process.exit(1);
}
console.log(`OK: ${testes} testes do front passaram`);
