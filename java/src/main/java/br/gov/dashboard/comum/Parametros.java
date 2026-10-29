package br.gov.dashboard.comum;

import br.gov.dashboard.modelo.Indicador;

import java.util.Map;

/**
 * Leitura dos parâmetros de query com a mesma semântica de api.c.
 *
 * Nada de {@code @RequestParam int ano}: a conversão automática do Spring
 * produziria o ProblemDetail dele em vez da mensagem "ano \"…\" inválido" que
 * o front sabe mostrar.
 */
public final class Parametros {

    private Parametros() {
    }

    public static Indicador indicador(Map<String, String> p) {
        Indicador ind = Indicador.de(p.get("indicador"));
        if (ind == null) throw new ApiException(400, "indicador inválido");
        return ind;
    }

    public static int ano(Map<String, String> p) {
        String bruto = p.get("ano");
        Integer v = Texto.inteiroEstrito(bruto);
        if (bruto == null || bruto.isEmpty() || v == null || v < 1900 || v > 2200) {
            throw new ApiException(400, "ano \"" + (bruto == null ? "" : bruto) + "\" inválido");
        }
        return v;
    }

    public static boolean nivelUf(Map<String, String> p) {
        String nivel = p.get("nivel");
        if (nivel == null || nivel.isEmpty()) return false;
        if (Texto.igualIgnorandoCaixa(nivel, "uf")) return true;
        if (Texto.igualIgnorandoCaixa(nivel, "municipio")
                || Texto.igualIgnorandoCaixa(nivel, "município")) return false;
        throw new ApiException(400,
                "nível \"" + nivel + "\" inválido: use municipio ou uf");
    }

    /** Limite malformado vira 0 (sem limite), como no C: não derruba a consulta. */
    public static int limite(Map<String, String> p) {
        String bruto = p.get("limite");
        if (bruto == null || bruto.isEmpty()) return 0;
        Integer v = Texto.inteiroEstrito(bruto);
        return v == null ? 0 : v;
    }

    /** Só 1/true/sim pedem nomes; "0", "false", "nao" e vazio pedem a resposta curta. */
    public static boolean nomes(Map<String, String> p) {
        String bruto = p.get("nomes");
        return bruto != null
                && (bruto.equals("1")
                || Texto.igualIgnorandoCaixa(bruto, "true")
                || Texto.igualIgnorandoCaixa(bruto, "sim"));
    }
}
