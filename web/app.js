/* web/app.js — orquestração do front.
 *
 * O requisito que organiza este arquivo: **nada é buscado nem renderizado até
 * o usuário clicar em "Filtrar"**. A página abre em branco, com os KPIs em
 * "--", o aviso de gráfico e o aviso de mapa que estão escritos no próprio
 * HTML. Nenhum fetch sai daqui até `filtrar()` ser chamado.
 *
 * Por isso a criação da aplicação (`criarApp`) não faz requisição nenhuma. O
 * que ela faz é ligar o botão e popular listas *estáticas* (fonte, indicador,
 * nível, ordenação, limite) — que são catálogo da própria interface, não dado
 * do servidor. UF e ano só são preenchidos depois do primeiro `filtrar()`,
 * porque quem os lista é /api/opcoes.
 *
 * `doc` e `fetch` são parâmetros, não globais. Isso é o que permite testar o
 * comportamento em node com um fetch falso e um DOM mínimo, sem jsdom — e é a
 * mesma separação do ruling C-5 que vale para os gráficos.
 */

import { consultar, opcoes, geo, montarQuery as montarQueryApi } from './api.js';
import {
    barras as desenharBarrasNo,
    donut as desenharDonutNo,
    kpi as desenharKpiNo,
    linha as desenharLinhaNo,
    paresDaSerie,
    publicacaoDoAno,
    fatiasDonut,
} from './charts.js';
import {
    OPCOES_FONTE,
    OPCOES_INDICADOR,
    OPCOES_NIVEL,
    OPCOES_ORDENAR,
    ORDENS,
    asValidas,
    indicadoresDaFonte,
    indicadorPadrao,
    inteiroValido,
    textoOpcoes,
    plural,
    paramsDaConsulta,
    preencher,
    preencherOpcoes,
    preencherIndicadores,
    preencherNiveis,
    preencherOrdenacoes,
    preencherLimites,
    lerFormulario,
} from './filters.js';

/* Reexportados porque o contrato do front é `window.Dashboard`, e os testes
 * importam daqui. Só os que vêm de outro módulo: os que são definidos aqui
 * (`estadoInicial`, `estadoVazio`, `lerEstado`, `anoValido`)
 * já são exports deste arquivo, e reexportá-los daria "duplicate export". */
export {
    montarQueryApi,
    asValidas,
    textoOpcoes,
    plural,
    paramsDaConsulta,
    OPCOES_FONTE,
    OPCOES_INDICADOR,
    OPCOES_NIVEL,
    OPCOES_ORDENAR,
    ORDENS,
    indicadoresDaFonte,
    indicadorPadrao,
    inteiroValido,
};

/**
 * Rótulo com acento de um indicador, a partir do valor que vai na query.
 *
 * `populacao` é o que o servidor entende; "População" é o que se lê. Trocar
 * os dois no meio do caminho põe texto sem acento na tela, que é justamente o
 * defeito que a restrição de português com acentos proíbe.
 */
export function rotuloIndicador(valor) {
    const achado = OPCOES_INDICADOR.find((o) => o.valor === valor);
    return achado ? achado.rotulo : (valor || 'indicador');
}

/** Estado da tela logo ao abrir: nada consultado, nada carregando. */
export function estadoInicial() {
    return {
        consultado: false,
        carregando: false,
        erro: null,
        fonte: 'ibge',
        indicador: null,
        nivel: 'municipio',
        ano: null,
        uf: '',
        total: 0,
        /* `registros` é o nome da spec §8: a resposta traz a tabela em
         * `registros`, e o front consome o contrato, não um apelido. */
        registros: [],
        kpis: null,
        serie: null,
        /* `seriePontos` é a mesma série já casada com os anos, em `{r, v}`.
         * É o que o gráfico consome: sem o ano ao lado, cada valor viraria
         * posição e o eixo encolheria a lacuna sem dizê-lo. */
        seriePontos: [],
        opcoesCarregadas: false,
    };
}

/**
 * O estado "vazio" do ponto de vista da tela: o que o usuário está vendo
 * enquanto `consultado` é falso. Serve para o renderizador saber que deve
 * deixar o HTML intocado, e não preencher nada.
 */
export function estadoVazio() {
    return { consultado: false, carregando: false, erro: null };
}

/**
 * Ano lido do formulário, com a regra da spec: "" e ".." viram `null`, nunca 0.
 *
 * Fica aqui, e não só em filters.js, porque é parte do contrato de estado —
 * o app precisa saber que não há ano antes de montar a query.
 */
export function anoValido(valor) {
    return asValidas(valor);
}

/** Snapshot defensivo do estado: quem recebe não pode mexer no interno. */
export function lerEstado(interno) {
    return {
        consultado: interno.consultado,
        carregando: interno.carregando,
        erro: interno.erro,
        fonte: interno.fonte,
        indicador: interno.indicador,
        nivel: interno.nivel,
        ano: interno.ano,
        uf: interno.uf,
        total: interno.total,
        registros: interno.registros.slice(),
        kpis: interno.kpis ? { ...interno.kpis } : null,
        serie: interno.serie ? interno.serie.slice() : null,
        seriePontos: interno.seriePontos.slice(),
        opcoesCarregadas: interno.opcoesCarregadas,
    };
}

/**
 * Cria a aplicação.
 *
 * @param {object}  opcoes
 * @param {object}  opcoes.doc     documento (injetado; no browser é `document`)
 * @param {Function} opcoes.fetch  fetch (injetado; no browser é o global)
 * @param {object|null} opcoes.logger  receptor de avisos; null silencia tudo.
 *        A spec manda **não** usar o console: erro de consulta é informação
 *        do usuário e vai para a tela. Por isso o padrão aqui é `null`, e
 *        não console.
 */
export function criarApp({ doc, fetch: fetchFn, logger = null } = {}) {
    if (!doc) throw new Error('criarApp precisa de um doc');

    const estado = estadoInicial();
    const cacheGeo = new Map();

    /* Evita duas consultas simultâneas: o botão fica desabilitado, mas
     * `filtrar()` é público e pode ser chamado duas vezes. */
    let emVoo = null;

    function avisar(mensagem) {
        if (logger && typeof logger.aviso === 'function') logger.aviso(mensagem);
    }

    /* ---------------------------------------------------------------- */
    /* Leitura do formulário e montagem da query                         */
    /* ---------------------------------------------------------------- */

    function formulario() {
        return lerFormulario(doc);
    }

    function paramsDeConsulta(f) {
        return paramsDaConsulta(f);
    }

    /* ---------------------------------------------------------------- */
    /* Render                                                            */
    /* ---------------------------------------------------------------- */

    /* Escreve texto em um painel. `texto` vazio devolve o painel ao estado
     * inicial do HTML em vez de deixar o texto anterior na tela. */
    function escreve(id, texto, classe) {
        const el = doc.getElementById(id);
        if (!el) return null;
        el.textContent = texto;
        if (classe !== undefined) el.className = classe;
        return el;
    }

    /**
     * Estado vazio: devolve os painéis ao que o HTML já escreve.
     *
     * Não é preciso "limpar" nada — os placeholders estão no HTML e nada foi
     * renderizado ainda. Esta função existe para quando uma consulta é
     * desfeita (fonte trocada, filtro mudado), e é onde a página volta ao
     * branco sem buscar nada.
     */
    function renderVazio() {
        const vazio = 'estado-vazio';
        const kpis = doc.getElementById('kpis');
        if (kpis) {
            /* Os KPIs do HTML são quatro cartões com "--". */
            for (const cartao of Array.from(kpis.querySelectorAll ? kpis.querySelectorAll('.kpi') : [])) {
                const valor = cartao.querySelector ? cartao.querySelector('.kpi-valor') : null;
                if (valor) valor.textContent = '--';
            }
            kpis.className = vazio;
        }
        escreve('graficos', 'Ajuste os filtros e clique em Filtrar', vazio);
        escreve('mapa', 'Sem dados — clique em Filtrar', vazio);
        return null;
    }

    /** Preenche os quatro KPIs. Valores ausentes viram "--", não 0. */
    function renderKpis(k, nivel) {
        const painel = doc.getElementById('kpis');
        if (!painel) return;
        const src = k || {};
        /* O rótulo do primeiro KPI acompanha o nível escolhido: com
         * `nivel=uf` o recorte é de estados, e escrever "Total de municípios"
         * sobre um total de estados seria um erro de português que também mente
         * sobre o conteúdo do cartão. A unidade vai embutida no rótulo, e não
         * no campo `unidade`, porque senão o valor sai como "1 municípios" —
         * o singular e o plural não podem ser a mesma string. */
        const campos = [
            [`Total de ${nivel === 'uf' ? 'estados' : 'municípios'}`, src.total, ''],
            ['Mínimo', src.min, ''],
            ['Máximo', src.max, ''],
            ['Média', src.media, ''],
        ];
        /* Reaproveita os cartões que já existem no HTML e só troca o texto,
         * para não reconstruir a estrutura a cada consulta. A escrita é
         *delegada ao `kpi` de charts.js, que é quem sabe que ausente é
         * "--": duas implementações de "como se escreve um número" são duas
         * chances de uma delas escrever 0 onde não há dado.
         *
         * Cada campo é [rótulo, valor, unidade] e a **posição** é o que
         * importa, então a desestruturação é por índice. Com `{ rotulo }`
         * sobre um array o nome nunca casa, o índice sai -1, e o laço inteiro
         * vira `continue` silencioso: os KPIs ficavam "--" mesmo com dado na
         * mão, sem erro nenhum no console. */
        const cartoes = painel.querySelectorAll ? painel.querySelectorAll('.kpi') : [];
        campos.forEach(([rotulo, valor, unidade], idx) => {
            const existente = cartoes[idx] || null;
            const cartao = desenharKpiNo(existente, { rotulo, valor, unidade }, { doc });
            /* Um campo a mais do que o HTML prevê não pode sumir em silêncio:
             * o cartão é criado e entra no painel. */
            if (!existente && cartao) painel.appendChild(cartao);
        });
        painel.className = '';
        return painel;
    }

    /** Tabela com os registros da consulta. */
    function renderTabela(registros) {
        const alvo = doc.getElementById('tabela');
        if (!alvo) return;
        while (alvo.firstChild) alvo.removeChild(alvo.firstChild);
        if (!registros || !registros.length) {
            const aviso = doc.createElement('p');
            aviso.textContent = 'Nenhum município para estes filtros.';
            alvo.appendChild(aviso);
            return;
        }
        const tabela = doc.createElement('table');
        const cab = doc.createElement('thead');
        const linhaCab = doc.createElement('tr');
        for (const titulo of ['Município', 'Código', 'Valor']) {
            const th = doc.createElement('th');
            th.textContent = titulo;
            linhaCab.appendChild(th);
        }
        cab.appendChild(linhaCab);
        tabela.appendChild(cab);

        const corpo = doc.createElement('tbody');
        for (const d of registros) {
            const tr = doc.createElement('tr');
            const nome = doc.createElement('td');
            nome.textContent = d && d.nome ? d.nome : '--';
            const cod = doc.createElement('td');
            cod.textContent = d && d.cod ? d.cod : '--';
            const valor = doc.createElement('td');
            valor.textContent = textoOpcoes(d && typeof d.valor === 'number' ? d.valor : null);
            tr.appendChild(nome);
            tr.appendChild(cod);
            tr.appendChild(valor);
            corpo.appendChild(tr);
        }
        tabela.appendChild(corpo);
        alvo.appendChild(tabela);
        return alvo;
    }

    /**
     * Cartão de gráfico: título, nota e a caixa onde o SVG entra.
     *
     * A nota carrega o recorte ("10 maiores de 5.570 municípios") porque um
     * gráfico sem recorte é um gráfico que promete mais do que mostra: dez
     * barras pareciam o país inteiro se ninguém dissesse que são os dez
     * maiores.
     */
    function cartao(titulo, nota) {
        const artigo = doc.createElement('article');
        artigo.className = 'grafico';
        const topo = doc.createElement('header');
        topo.className = 'grafico-topo';
        const h = doc.createElement('h3');
        h.className = 'grafico-titulo';
        h.textContent = titulo;
        topo.appendChild(h);
        if (nota) {
            const p = doc.createElement('p');
            p.className = 'grafico-nota';
            p.textContent = nota;
            topo.appendChild(p);
        }
        const corpo = doc.createElement('div');
        corpo.className = 'grafico-corpo';
        artigo.appendChild(topo);
        artigo.appendChild(corpo);
        return { artigo, corpo };
    }

    /**
     * Os gráficos da consulta.
     *
     * Três painéis, e cada um só aparece quando tem o que mostrar:
     *
     *   - a linha é a série histórica, e ela só existe quando a resposta
     *     traz `serie` **e** `serie_anos` — o servidor só a devolve em recorte
     *     por UF. Sem recorte, o painel diz que é preciso escolher uma UF, em
     *     vez de inventar um eixo de anos;
     *   - as barras são os maiores valores da resposta, com o traço de "sem
     *     dado" no lugar do município que não tem valor publicado;
     *   - o anel é a cobertura do ano: quantos municípios têm dado e quantos
     *     não têm, que é a mesma informação da spec §6 que a linha expressa
     *     no tempo.
     */
    function renderGraficos(resposta, f) {
        const painel = doc.getElementById('graficos');
        if (!painel) return null;
        while (painel.firstChild) painel.removeChild(painel.firstChild);

        /* O indicador na tela é o rótulo com acento, não o token que vai na
         * query: "populacao" lido em tela é a fonte sem acento voltando. */
        const rotulo = rotuloIndicador(f.indicador);
        const recorte = recorteDe(f);
        const total = estado.total;
        const registros = estado.registros;
        const pontos = estado.seriePontos;
        const kpis = estado.kpis || {};

        /* --- série histórica ------------------------------------------- */
        const cartaoLinha = cartao(
            `${rotulo} ao longo do tempo${recorte ? ` — ${recorte}` : ''}`,
            pontos.length
                ? `${pontos.length} anos publicados${f.ano ? ` · destaque em ${f.ano}` : ''}`
                : 'Sem série neste recorte: escolha uma UF para ver a história.',
        );
        painel.appendChild(cartaoLinha.artigo);
        desenharLinhaNo(cartaoLinha.corpo, pontos, {
            doc,
            titulo: `${rotulo} por ano${recorte ? `, ${recorte}` : ''}`,
            cor: 'var(--acento)',
            semSerie: 'Escolha uma UF no filtro para ver a série deste lugar ao longo do tempo.',
        });

        /* --- maiores valores -------------------------------------------- */
        const quantos = registros.length > 10 ? 10 : registros.length;
        const unidade = f.nivel === 'uf' ? 'estado' : 'município';
        const cobertura = quantos < total
            ? `${quantos} maiores de ${textoOpcoes(total)} ${plural(total, unidade, `${unidade}s`)}`
            : (quantos === 1 ? `o único ${unidade} do resultado` : `os ${quantos} do resultado`);
        const cartaoBarras = cartao(`${rotulo} por localidade`, cobertura);
        painel.appendChild(cartaoBarras.artigo);
        desenharBarrasNo(cartaoBarras.corpo, registros, {
            doc,
            titulo: `${rotulo} por localidade`,
            maximo: 10,
            cor: true,
            semDados: 'Nenhuma localidade com dado neste recorte.',
        });

        /* --- cobertura do ano ------------------------------------------- */
        /* `unidade` aqui é a que o anel desenha no centro — maiúscula, porque é
         * um rótulo. O texto do cartão é outro, em minúscula e com o número
         * deciding o plural: com um estado no resultado, "1 estados no
         * resultado" aparecia na tela. */
        const unidadeAnel = f.nivel === 'uf' ? 'Estados' : 'Municípios';
        const fatias = publicacaoDoAno(total, kpis.com_dado, unidadeAnel);
        const cartaoAnel = cartao(`Dado publicado em ${f.ano || '—'}`,
            `${textoOpcoes(total)} ${plural(total, f.nivel === 'uf' ? 'estado' : 'município', f.nivel === 'uf' ? 'estados' : 'municípios')} no resultado`);
        painel.appendChild(cartaoAnel.artigo);
        desenharDonutNo(cartaoAnel.corpo, fatiasDonut(fatias), {
            doc,
            tamanho: 150,
            titulo: `Dado publicado em ${f.ano || '—'}`,
            semDados: 'Sem resultado para medir a cobertura.',
        });

        painel.className = '';
        return painel;
    }

    /** "SP · por UF", a partir do que está no formulário. */
    function recorteDe(f) {
        const partes = [];
        if (f.uf) partes.push(f.uf);
        if (f.nivel === 'uf') partes.push('por UF');
        return partes.join(' · ');
    }

    /** O mapa é cinza neutro até haver valor para colorir. */
    function renderMapa(resposta, nivel) {
        const painel = doc.getElementById('mapa');
        if (!painel) return;
        while (painel.firstChild) painel.removeChild(painel.firstChild);
        const total = resposta && typeof resposta.total === 'number' ? resposta.total : 0;
        if (total === 0) {
            painel.textContent = 'Sem dados para os filtros escolhidos.';
            painel.className = 'estado-vazio';
            return painel;
        }
        const p = doc.createElement('p');
        const unidade = nivel === 'uf' ? 'estado' : 'município';
        p.textContent = `${textoOpcoes(total)} ${plural(total, unidade, `${unidade}s`)} no resultado.`;
        painel.appendChild(p);
        painel.className = '';
        return painel;
    }

    function renderErro(mensagem) {
        /* O erro vai para #kpis, na tela. Nunca para o console: quem precisa
         * ler a mensagem é a pessoa na frente do navegador. */
        escreve('kpis', mensagem, 'erro');
        estado.erro = mensagem;
        return null;
    }

    function renderSpinner(ligado) {
        const botao = doc.getElementById('filtrar');
        if (botao) {
            botao.disabled = !!ligado;
            const rotulo = botao.querySelector ? botao.querySelector('.rotulo') : null;
            if (rotulo) rotulo.textContent = ligado ? 'Consultando…' : 'Filtrar';
        }
        estado.carregando = !!ligado;
        return !!ligado;
    }

    /* ---------------------------------------------------------------- */
    /* Opções (depois do clique, nunca antes)                            */
    /* ---------------------------------------------------------------- */

    /**
     * Carrega /api/opcoes uma vez por (fonte, indicador) e preenche #uf e
     * #ano com o que veio. Chamado de dentro de `filtrar()`, nunca no load.
     *
     * Falha aqui **não** cancela a consulta: as opções servem para preencher
     * os seletores, e o ano que o usuário já digitou não depende delas.
     * Bloquear a consulta porque /api/opcoes estava fora tiraria do usuário a
     * única coisa que ele pediu. Nesse caso volta `null` e a consulta segue
     * com o que está no formulário.
     */
    async function garantirOpcoes(f) {
        const chave = `${f.fonte}|${f.indicador}`;
        if (estado.opcoesCarregadas && estado.chaveOpcoes === chave) {
            return { resultado: estado.opcoes, ano: null, uf: null };
        }
        let r;
        try {
            r = await opcoes(f.fonte, f.indicador, { fetch: fetchFn });
        } catch (e) {
            avisar(`não foi possível carregar as opções: ${e && e.message ? e.message : 'erro'}`);
            return null;
        }
        estado.opcoesCarregadas = true;
        estado.chaveOpcoes = chave;
        estado.opcoes = r;
        const alvo = preencherOpcoes(doc, r, anoValido(f.ano), f.uf || '');
        return { resultado: r, ano: alvo.ano, uf: alvo.uf };
    }

    /* ---------------------------------------------------------------- */
    /* Ações                                                             */
    /* ---------------------------------------------------------------- */

    /**
     * Trocar a fonte troca os indicadores oferecidos e volta a tela ao vazio.
     *
     * Não busca nada: o usuário pode navegar entre fontes sem ainda ter
     * pedido dado nenhum. O que estava desenhado é de outra fonte e sai de
     * cena.
     */
    async function definirFonte(fonte) {
        const valida = OPCOES_FONTE.some((f) => f.valor === fonte);
        if (!valida) return estado;
        estado.fonte = fonte;
        const selFonte = doc.getElementById('fonte');
        if (selFonte) selFonte.value = fonte;
        estado.indicador = preencherIndicadores(doc, fonte, estado.indicador);
        /* A fonte trocou, então as opções da antiga não valem mais. */
        estado.opcoesCarregadas = false;
        estado.chaveOpcoes = null;
        /* A tela volta ao branco: o que estava desenhado era de outra fonte. */
        renderVazio();
        return lerEstado(estado);
    }

    /**
     * O botão "Filtrar". É o único caminho para a rede.
     *
     * Ordem: lê o formulário, busca as opções (para #uf e #ano terem o que o
     * servidor tem), desabilita o botão, consulta, e só então renderiza.
     */
    async function filtrar() {
        if (emVoo) return emVoo;

        emVoo = (async () => {
            let f = formulario();
            renderSpinner(true);
            try {
                const alvo = await garantirOpcoes(f);
                /* Se o formulário não tinha ano válido e o servidor oferecia
                 * algum, usa o mais recente. Nunca inventa um ano — se nem o
                 * formulário nem as opções têm um, `ano` fica null e o
                 * parâmetro simplesmente não vai na query. */
                const digitado = anoValido(f.ano);
                const ano = digitado !== null ? digitado : (alvo ? anoValido(alvo.ano) : null);

                /* Sem ano não há consulta possível, e a que o servidor faria
                 * seria recusada com um 400. Melhor dizer por que logo aqui:
                 * deixar "ano inválido" na tela não ajuda ninguém a
                 * descobrir que o motivo é não haver dado processado. */
                if (ano === null) {
                    return renderErro(
                        'Nenhum ano disponível para consultar. '
                        + 'Verifique se o modelo foi processado (./dashboard --atualizar).'
                    );
                }

                const uf = f.uf || (alvo && alvo.uf) || '';
                const consulta = { ...f, ano: ano === null ? '' : String(ano), uf };

                estado.erro = null;
                const resposta = await consultar(paramsDeConsulta(consulta), { fetch: fetchFn });

                estado.consultado = true;
                estado.ano = ano;
                estado.uf = uf;
                estado.indicador = consulta.indicador;
                estado.nivel = consulta.nivel;
                /* A spec §8: `total` é o do conjunto (não o da página),
                 * `kpis` é um objeto e `registros` é a tabela. Ler o `total`
                 * de dentro de `kpis`, que é onde ele estava, fazia o cartão
                 * dizer o número de linhas da página. */
                estado.total = resposta && typeof resposta.total === 'number' ? resposta.total : 0;
                estado.registros = (resposta && Array.isArray(resposta.registros))
                    ? resposta.registros : [];
                estado.kpis = resposta && resposta.kpis && typeof resposta.kpis === 'object'
                    ? resposta.kpis : null;
                estado.serie = resposta && Array.isArray(resposta.serie) ? resposta.serie : null;
                /* A série só existe com recorte (a spec §8 e a decisão
                 * documentada em api.c: sem recorte, a curva do Brasil inteiro
                 * custaria 600 KB por consulta). O `paresDaSerie` é o que separa
                 * "sem série" de "série sem ano", e as duas precisam dar um
                 * gráfico honesto: a primeira diz para escolher uma UF, e a
                 * segunda jamais acontece. */
                estado.seriePontos = paresDaSerie(resposta);

                renderKpis({ total: estado.total, ...(estado.kpis || {}) }, consulta.nivel);
                renderGraficos(resposta, consulta);
                renderTabela(estado.registros);
                renderMapa(resposta, consulta.nivel);
                return lerEstado(estado);
            } catch (e) {
                estado.consultado = false;
                return renderErro(e && e.message ? e.message : 'Falha na consulta');
            } finally {
                renderSpinner(false);
                emVoo = null;
            }
        })();

        return emVoo;
    }

    /** Leitura de estado, sem efeito colateral e sem buscar nada. */
    function estado_() {
        return lerEstado(estado);
    }

    /* ---------------------------------------------------------------- */
    /* Ligação                                                            */
    /* ---------------------------------------------------------------- */

    const botao = doc.getElementById('filtrar');
    if (botao && typeof botao.addEventListener === 'function') {
        botao.addEventListener('click', (ev) => {
            if (ev && typeof ev.preventDefault === 'function') ev.preventDefault();
            /* Devolve a promessa: quem dispara o clique precisa poder esperar
             * a consulta terminar, senão um teste lê o estado antes de ele
             * existir. */
            return filtrar();
        });
    }

    const selFonte = doc.getElementById('fonte');
    if (selFonte && typeof selFonte.addEventListener === 'function') {
        selFonte.addEventListener('change', () => definirFonte(selFonte.value));
    }

    /* Estado inicial das listas estáticas: catálogo da interface, não dado do
     * servidor. #uf e #ano ficam de fora — quem os lista é /api/opcoes, e
     * buscá-los aqui seria um fetch sem o clique. */
    preencher(doc, selFonte, OPCOES_FONTE, estado.fonte);
    preencherIndicadores(doc, estado.fonte, estado.indicador);
    preencherNiveis(doc, estado.nivel);
    preencherOrdenacoes(doc, ORDENS[0].codigo);
    preencherLimites(doc, null);

    return { filtrar, estado: estado_, definirFonte, geo: (n) => geo(n, { fetch: fetchFn, cache: cacheGeo }) };
}

/* ------------------------------------------------------------------ */
/* Ponte com o browser                                                  */
/* ------------------------------------------------------------------ */

/**
 * Liga a aplicação ao `window` quando existe um documento real.
 *
 * Este é o único ponto do módulo que conhece o ambiente global, e ele é
 * inerte em node — o import não faz nada, o que mantém os testes sem DOM.
 */
if (typeof window !== 'undefined' && typeof document !== 'undefined') {
    window.Dashboard = criarApp({ doc: document, fetch: (...a) => window.fetch(...a) });
}
