/* tests/fake_dom.mjs — o DOM falso que os testes do front usam.
 *
 * app.js não toca em `document` no import: quem monta a interface recebe o
 * `doc` por parâmetro. Isso permite rodar o front inteiro em node, sem jsdom
 * e sem navegador, e é o que torna possível testar as duas coisas que a spec
 * exige: que nada seja buscado antes do clique, e que valor ausente vire
 * "--" e não 0.
 *
 * Este arquivo existe porque havia DUAS cópias desse DOM — uma em
 * test_web.mjs e outra em front_e2e.mjs — e a segunda estava incompleta: sem
 * `value` nos `<select>`, sem os quatro cartões `.kpi` e sem `click()`. O
 * sintoma era o app renderizando "Nenhum ano disponível para consultar"
 * depois de uma consulta que tinha dado ano, e um `Cannot read properties of
 * undefined (reading 'slice')` em três passos. Um stub permissivo demais não
 * testa nada; um stub pela metade testa o stub.
 *
 * O `querySelectorAll` percorre a árvore de verdade e casa por classe. A
 * primeira versão devolvia `[]` para tudo e escondeu um bug real: os KPIs
 * ficavam em "--" mesmo com dado, porque o laço de render nunca achava o
 * cartão. Stub que aceita qualquer coisa não detecta nada.
 */

/** Um elemento. Só o que app.js e filters.js tocam. */
export function criaEl(tag, doc) {
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
        /* O app desenha em canvas. Aqui não há pixels, e um objeto vazio
         * basta: o que se mede é dado, não raster. */
        getContext() {
            return new Proxy({}, {
                get(alvo, prop) {
                    if (prop === 'canvas') return { width: 800, height: 400 };
                    if (prop === 'measureText') return () => ({ width: 10 });
                    if (prop === 'createLinearGradient') return () => ({ addColorStop() {} });
                    return () => {};
                },
                set() { return true; },
            });
        },
    };
}

/**
 * Monta a mesma estrutura de KPIs que está no index.html, para que o teste
 * exercite o caminho de render de verdade em vez de um atalho.
 */
export function criaPainelKpis(doc) {
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

/** Os ids que o index.html tem e o front procura. */
export const IDS = ['fonte', 'uf', 'ano', 'indicador', 'nivel', 'busca', 'ordenar',
    'limite', 'filtrar', 'kpis', 'graficos', 'tabela', 'mapa', 'estado-vazio'];

/**
 * Cria o documento falso.
 *
 * Os `<select>` e o `<input>` nascem com `tagName` diferente porque o app
 * trata campo de texto e lista de opções por caminhos distintos.
 */
export function criaDoc(ids = IDS) {
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
        const el = criaEl('div', doc);
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

/**
 * Deixa os filtros no estado que alguém teria escolhido na tela.
 *
 * Os valores padrão são os mesmos que o `index.html` mostra: IBGE,
 * população, nível município, ordenação por valor, 100 linhas e 2024.
 */
export function preencheFiltros(doc, valores = {}) {
    Object.assign(doc.porId.fonte, { value: valores.fonte ?? 'ibge' });
    Object.assign(doc.porId.indicador, { value: valores.indicador ?? 'populacao' });
    Object.assign(doc.porId.nivel, { value: valores.nivel ?? 'municipio' });
    Object.assign(doc.porId.ordenar, { value: valores.ordenar ?? 'valor_desc' });
    Object.assign(doc.porId.limite, { value: valores.limite ?? '100' });
    Object.assign(doc.porId.ano, { value: valores.ano ?? '2024' });
    Object.assign(doc.porId.uf, { value: valores.uf ?? '' });
    Object.assign(doc.porId.busca, { value: valores.busca ?? '' });
    return doc;
}

/** Documento pronto para uso: ids + cartões de KPI + filtros preenchidos. */
export function docDeTeste(valores = {}) {
    const doc = criaDoc();
    criaPainelKpis(doc);
    preencheFiltros(doc, valores);
    return doc;
}