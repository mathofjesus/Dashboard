/* web/api.js — chamadas HTTP do front.
 *
 * Nada aqui toca em `document`: este módulo é importável em node, e é o que
 * permite testar o comportamento da consulta sem navegador. Quem chama passa
 * o `fetch` e o `cache`; o padrão é o global do navegador.
 *
 * Duas decisões que valem explicação:
 *
 * - Erro do servidor vira exceção com a mensagem que ele mandou. A API
 *   responde `{"erro":{"codigo":…,"mensagem":…}}`, e essa `mensagem` é
 *   escrita na tela pelo app. Se encolhéssemos para "falhou", o usuário
 *   perderia "ano 2022 inexistente", que é a informação que ele precisa para
 *   corrigir o filtro.
 *
 * - Geometria é cacheada, valor nunca. A malha é o desenho do mapa; o que o
 *   dashboard colore muda a cada consulta. Cachear valor pré-carregaria a
 *   tela, que é o que a spec proíbe.
 */

/** Níveis que o servidor serve em /api/geo/. */
export const NIVEIS_GEO = ['estados', 'municipios'];

/**
 * O nome do nível no vocabulário do formulário → o da rota.
 *
 * O seletor de nível usa `municipio` e `uf` (é o que o servidor espera em
 * `nivel=`), e a rota de geometria usa `municipios` e `estados` (é o que o
 * geo.json e o endpoint chamam). Sem esta tradução, `geo('municipio')` jogava
 * "nível de mapa desconhecido" e a exceção subia para dentro de `filtrar()`,
 * que a tratava como erro da consulta — o cartão de KPIs virava a mensagem de
 * erro do mapa. Não apareceu antes porque nada chamava `geo()`.
 *
 * `uf` entra como sinônimo de `estados`: é o código que o seletor carrega, e
 * o aggregatesó-lo conhece.
 */
const NIVEL_GEO = {
    municipio: 'municipios',
    municipios: 'municipios',
    uf: 'estados',
    estado: 'estados',
    estados: 'estados',
};

/** Traduz o nome do nível; devolve null quando não é um nível conhecido. */
export function nivelGeo(nivel) {
    if (!nivel) return null;
    const chave = String(nivel).trim().toLowerCase();
    return Object.prototype.hasOwnProperty.call(NIVEL_GEO, chave) ? NIVEL_GEO[chave] : null;
}

/**
 * Escapa um valor para query string.
 *
 * `encodeURIComponent` já escapa espaço como %20 (não como +), e isso importa:
 * o servidor decodifica %XX e troca + por espaço, mas um `+` dentro de um nome
 * de município seria lido errado por qualquer coisa que não siga a mesma
 * regra. Uma função só, sem caso especial.
 */
export function esc(valor) {
    return encodeURIComponent(valor === undefined || valor === null ? '' : String(valor));
}

/**
 * Monta a query de /api/consulta.
 *
 * Recebe o objeto já validado (ou não) e devolve `""` ou `?a=b&c=d`. Chave
 * com valor ausente não entra: enviar `uf=` e `uf` ausente não é a mesma
 * coisa para quem lê do outro lado.
 */
export function montarQuery(params) {
    if (!params) return '';
    const partes = [];
    for (const chave of Object.keys(params)) {
        const v = params[chave];
        if (v === undefined || v === null) continue;
        if (v === '') continue;
        partes.push(`${esc(chave)}=${esc(v)}`);
    }
    return partes.length ? `?${partes.join('&')}` : '';
}

/** Caminho completo de uma consulta, sem host. */
export function rotaConsulta(params) {
    return `/api/consulta${montarQuery(params)}`;
}

function fetchDe(opts) {
    return (opts && opts.fetch) || globalThis.fetch;
}

/**
 * Lê o corpo JSON e transforma o envelope de erro em exceção.
 *
 * `res.ok` sozinho não basta: a API sinaliza erro no corpo (`codigo`, não
 * status HTTP) em alguns caminhos, e um 200 com `{"erro":…}` entregue ao
 * renderizador viraria tela em branco sem explicação.
 */
async function lerJson(res, caminho) {
    let corpo = null;
    let parseou = true;
    try {
        corpo = await res.json();
    } catch (e) {
        parseou = false;
    }

    if (corpo && typeof corpo === 'object' && corpo.erro) {
        const info = corpo.erro;
        const msg = (info && typeof info.mensagem === 'string' && info.mensagem.trim())
            ? info.mensagem
            : `falha em ${caminho}`;
        throw new Error(msg);
    }

    if (!res.ok) {
        if (parseou) {
            throw new Error(`HTTP ${res.status} em ${caminho}`);
        }
        throw new Error(`resposta ilegível (HTTP ${res.status}) em ${caminho}`);
    }

    if (!parseou) {
        throw new Error(`resposta sem JSON em ${caminho}`);
    }
    return corpo;
}

/**
 * GET /api/consulta.
 *
 * Devolve o envelope como veio do servidor. O app decide o que renderizar;
 * aqui só há transporte e atransformação de erro.
 */
export async function consultar(params, opts) {
    const f = fetchDe(opts);
    const caminho = rotaConsulta(params);
    const res = await f(caminho, { headers: { Accept: 'application/json' } });
    return lerJson(res, caminho);
}

/**
 * GET /api/opcoes.
 *
 * Normaliza os anos para número e **descarta** o que não for ano. O servidor
 * pode mandar "", null ou ".." em uma posição que ele mesmo não dokumenta; se
 * passassem adiante virariam 0 depois de um `parseInt` leniente, e o seletor
 * ofereceria um ano que não existe.
 */
export async function opcoes(fonte, ind, opts) {
    const f = fetchDe(opts);
    const caminho = `/api/opcoes${montarQuery({ fonte, indicador: ind })}`;
    const res = await f(caminho, { headers: { Accept: 'application/json' } });
    const corpo = await lerJson(res, caminho);
    const brutos = (corpo && corpo.anos) || [];
    const anos = [];
    for (const a of brutos) {
        const n = typeof a === 'number' ? a : parseInt(String(a).trim(), 10);
        if (!Number.isInteger(n) || n <= 0) continue;
        if (anos.includes(n)) continue;
        anos.push(n);
    }
    const ufs = (corpo && Array.isArray(corpo.ufs)) ? corpo.ufs.filter((u) => typeof u === 'string' && u) : [];
    return { anos, ufs };
}

/**
 * GET /api/valores — o par `codarea → valor` do recorte inteiro.
 *
 * Existe separada de `consultar` porque o mapa precisa de TODAS as localidades
 * e a consulta devolve a página: com `limite=100`, os outros 5470 municípios
 * eram pintados com a cor de ausência, que significa "sem dado", quando têm
 * dado e só não foram paginados.
 *
 * `nomes` pede o dicionário de codarea → nome, que o clique no mapa mostra.
 * O nome não muda com o filtro, então o app o pede uma vez e o memoriza; os
 * valores são buscados a cada consulta.
 */
export async function valores(params, opts = {}) {
    const f = fetchDe(opts);
    const todos = Object.assign({}, params);
    if (opts.nomes) todos.nomes = '1';
    const caminho = `/api/valores${montarQuery(todos)}`;
    const res = await f(caminho, { headers: { Accept: 'application/json' } });
    const corpo = await lerJson(res, caminho);
    return {
        total: corpo && typeof corpo.total === 'number' ? corpo.total : 0,
        com_dado: corpo && typeof corpo.com_dado === 'number' ? corpo.com_dado : 0,
        valores: (corpo && corpo.valores) || {},
        nomes: (corpo && corpo.nomes) || null,
    };
}

/**
 * GET /api/geo/{nivel}.
 *
 * A malha é o desenho do mapa, não dado: pode ser buscada uma vez e
 * memorizada, e é por isso que ela aceita `cache`. Devolve `null` em vez de
 * lançar quando não está disponível — sem dado processado a rota responde
 * 503, e um mapa ausente não pode derrubar os KPIs que já responderam.
 */
export async function geo(nivel, opts) {
    const alvo = nivelGeo(nivel);
    if (!alvo) {
        throw new Error(`nível de mapa desconhecido: ${nivel}`);
    }
    const cache = opts && opts.cache;
    const chave = `geo:${alvo}`;
    if (cache && cache.has(chave)) return cache.get(chave);

    const f = fetchDe(opts);
    const caminho = `/api/geo/${alvo}`;
    try {
        const res = await f(caminho, { headers: { Accept: 'application/geo+json' } });
        const corpo = await lerJson(res, caminho);
        if (cache) cache.set(chave, corpo);
        return corpo;
    } catch (e) {
        return null;
    }
}
