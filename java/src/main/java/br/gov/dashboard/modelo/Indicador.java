package br.gov.dashboard.modelo;

import br.gov.dashboard.comum.Texto;

/** Os cinco indicadores do modelo. A ordem é a do arquivo binário. */
public enum Indicador {

    POPULACAO("populacao"),
    ESTABELECIMENTOS("estabelecimentos"),
    LEITOS("leitos"),
    UTI("uti"),
    DENGUE("dengue");

    public static final int TOTAL = 5;

    private final String nome;

    Indicador(String nome) {
        this.nome = nome;
    }

    public String nome() {
        return nome;
    }

    /**
     * Ausente é população (o único indicador com dado hoje). Desconhecido
     * devolve {@code null}; quem chama transforma em 400 "indicador inválido".
     */
    public static Indicador de(String s) {
        if (s == null || s.isEmpty()) return POPULACAO;
        if (Texto.igualIgnorandoCaixa(s, "populacao")
                || Texto.igualIgnorandoCaixa(s, "população")) return POPULACAO;
        if (Texto.igualIgnorandoCaixa(s, "estabelecimentos")) return ESTABELECIMENTOS;
        if (Texto.igualIgnorandoCaixa(s, "leitos")) return LEITOS;
        if (Texto.igualIgnorandoCaixa(s, "uti")) return UTI;
        if (Texto.igualIgnorandoCaixa(s, "dengue")) return DENGUE;
        return null;
    }

    public int codigo() {
        return ordinal();
    }
}
