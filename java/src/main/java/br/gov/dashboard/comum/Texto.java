package br.gov.dashboard.comum;

/**
 * Texto com a mesma semântica do C: comparação e busca são ASCII-only e a
 * ordenação é por bytes UTF-8, não por unidades UTF-16 do Java.
 *
 * Isso não é preciosismo: {@code String.equalsIgnoreCase} casaria "POPULAÇÃO"
 * com "população", e o C não casa; {@code String.compareTo} ordena "Água" e
 * "Zeta" em ordem diferente de {@code strcmp} sobre UTF-8. O contrato da API e
 * a ordem da tabela precisam continuar iguais.
 */
public final class Texto {

    private Texto() {
    }

    public static char minusculo(char c) {
        return (c >= 'A' && c <= 'Z') ? (char) (c + ('a' - 'A')) : c;
    }

    public static boolean igualIgnorandoCaixa(String a, String b) {
        if (a == null) a = "";
        if (b == null) b = "";
        if (a.length() != b.length()) return false;
        for (int i = 0; i < a.length(); i++) {
            if (minusculo(a.charAt(i)) != minusculo(b.charAt(i))) return false;
        }
        return true;
    }

    /** Equivalente a {@code contem_ci} de aggregate.c. */
    public static boolean contemIgnorandoCaixa(String palheiro, String agulha) {
        if (agulha == null || agulha.isEmpty()) return true;
        if (palheiro == null) return false;
        int k = agulha.length();
        for (int i = 0; i + k <= palheiro.length(); i++) {
            int j = 0;
            while (j < k && minusculo(palheiro.charAt(i + j)) == minusculo(agulha.charAt(j))) {
                j++;
            }
            if (j == k) return true;
        }
        return false;
    }

    /** Equivalente a {@code strcmp} sobre os bytes UTF-8 (comparação sem sinal). */
    public static int compararBytes(String a, String b) {
        byte[] ba = a.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        byte[] bb = b.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        int n = Math.min(ba.length, bb.length);
        for (int i = 0; i < n; i++) {
            int ca = ba[i] & 0xff;
            int cb = bb[i] & 0xff;
            if (ca != cb) return ca - cb;
        }
        return ba.length - bb.length;
    }

    /**
     * Inteiro estrito no espírito de {@code strtol} base 10: espaço inicial é
     * aceito, sinal é aceito, nada depois dos dígitos é aceito. Devolve
     * {@code null} quando o texto não é um inteiro representável em int.
     */
    public static Integer inteiroEstrito(String s) {
        if (s == null) return null;
        int i = 0, n = s.length();
        while (i < n && Character.isWhitespace(s.charAt(i))) i++;
        int inicio = i;
        if (i < n && (s.charAt(i) == '+' || s.charAt(i) == '-')) i++;
        int digitos = i;
        while (i < n && s.charAt(i) >= '0' && s.charAt(i) <= '9') i++;
        if (i == digitos) return null;
        if (i != n) return null;
        try {
            long v = Long.parseLong(s.substring(inicio));
            if (v < Integer.MIN_VALUE || v > Integer.MAX_VALUE) return null;
            return (int) v;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public static String vazioParaNulo(String s) {
        return (s == null || s.isEmpty()) ? null : s;
    }
}
