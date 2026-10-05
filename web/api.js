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
 * GET /api/geo/{nivel}.
 *
 * A malha é o desenho do mapa, não dado: pode ser buscada uma vez e
 * memorizada, e é por isso que ela aceita `cache`. Devolve `null` em vez de
 * lançar quando não está disponível — sem dado processado a rota responde
 * 503, e um mapa ausente não pode derrubar os KPIs que já responderam.
 */
export async function geo(nivel, opts) {
    if (!NIVEIS_GEO.includes(nivel)) {
        throw new Error(`nível de mapa desconhecido: ${nivel}`);
    }
    const cache = opts && opts.cache;
    const chave = `geo:${nivel}`;
    if (cache && cache.has(chave)) return cache.get(chave);

    const f = fetchDe(opts);
    const caminho = `/api/geo/${nivel}`;
    try {
        const res = await f(caminho, { headers: { Accept: 'application/geo+json' } });
        const corpo = await lerJson(res, caminho);
        if (cache) cache.set(chave, corpo);
        return corpo;
    } catch (e) {
        return null;
    }
}
