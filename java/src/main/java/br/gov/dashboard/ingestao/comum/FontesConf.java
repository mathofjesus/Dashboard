package br.gov.dashboard.ingestao.comum;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Leitor do {@code fontes.conf}, com a mesma gramática do C.
 *
 * O arquivo é a lista do que se baixa e de onde; as URLs do Ministério da
 * Saúde e do IBGE vivem nele, nunca no código. A leitura é deliberadamente
 * literal — seção entre colchetes, {@code chave = valor}, comentário com
 * {@code #} — porque o contrato é o arquivo que já existe, não um formato novo.
 */
public final class FontesConf {

    /** Uma seção do .conf. Campos ausentes ficam vazios, como no C. */
    public record Fonte(
            String id,
            String nome,
            String tipo,
            String url,
            String dataset,
            int ttl,
            boolean temUtf8) {

        public Fonte {
            id = id == null ? "" : id;
            nome = nome == null ? "" : nome;
            tipo = tipo == null ? "" : tipo;
            url = url == null ? "" : url;
            dataset = dataset == null ? "" : dataset;
        }
    }

    public static final int TTL_NENHUM = -1;

    private FontesConf() {
    }

    public static List<Fonte> ler(Path caminho) throws IOException {
        if (caminho == null || !Files.isRegularFile(caminho)) {
            throw new IOException("não consegui abrir " + caminho);
        }
        List<String> linhas = Files.readAllLines(caminho, StandardCharsets.UTF_8);
        List<Fonte> fontes = new ArrayList<>();

        String id = null, nome = null, tipo = null, url = null, dataset = null;
        int ttl = TTL_NENHUM;
        boolean temUtf8 = false;
        boolean dentroDeSecao = false;

        for (String bruta : linhas) {
            String p = trim(bruta);
            if (p.isEmpty()) continue;
            if (p.charAt(0) == '#') continue;

            if (p.charAt(0) == '[') {
                int fim = p.indexOf(']');
                if (fim < 0) continue;
                if (dentroDeSecao) {
                    fontes.add(new Fonte(id, nome, tipo, url, dataset, ttl, temUtf8));
                }
                id = trim(p.substring(1, fim));
                nome = tipo = url = dataset = null;
                ttl = TTL_NENHUM;
                temUtf8 = false;
                dentroDeSecao = true;
                continue;
            }

            int eq = p.indexOf('=');
            if (eq < 0) continue;
            if (!dentroDeSecao) continue;

            String chave = trim(p.substring(0, eq));
            String valor = trim(p.substring(eq + 1));
            switch (chave) {
                case "id" -> id = valor;
                case "nome" -> nome = valor;
                case "tipo" -> tipo = valor;
                case "url", "url_zip" -> url = valor;
                case "dataset" -> dataset = valor;
                case "tem_utf8" -> temUtf8 = atoi(valor) != 0;
                case "ttl" -> {
                    Integer v = inteiroEstrito(valor);
                    if (v != null) ttl = v;
                }
                default -> {
                    // Chaves que a migração ainda não usa (url_cnes etc.) são
                    // ignoradas, como o C faz para qualquer chave desconhecida.
                }
            }
        }
        if (dentroDeSecao) {
            fontes.add(new Fonte(id, nome, tipo, url, dataset, ttl, temUtf8));
        }
        if (fontes.isEmpty()) throw new IOException("fontes.conf sem nenhuma seção");
        return Collections.unmodifiableList(fontes);
    }

    public static Fonte obtem(List<Fonte> fontes, String id) {
        if (fontes == null || id == null) return null;
        for (Fonte f : fontes) {
            if (f.id().equals(id)) return f;
        }
        return null;
    }

    static String trim(String s) {
        int ini = 0, fim = s.length();
        while (ini < fim && ehEspaco(s.charAt(ini))) ini++;
        while (fim > ini && ehEspaco(s.charAt(fim - 1))) fim--;
        return s.substring(ini, fim);
    }

    private static boolean ehEspaco(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\u000B'
                || c == '\f' || c == '\r';
    }

    private static int atoi(String s) {
        Integer v = inteiroEstrito(s);
        return v == null ? 0 : v;
    }

    /** strtol base 10 com o texto inteiro consumido; null quando não é inteiro. */
    static Integer inteiroEstrito(String s) {
        if (s == null || s.isEmpty()) return null;
        int i = 0, n = s.length();
        while (i < n && ehEspaco(s.charAt(i))) i++;
        int inicio = i;
        if (i < n && (s.charAt(i) == '+' || s.charAt(i) == '-')) i++;
        int digitos = i;
        while (i < n && s.charAt(i) >= '0' && s.charAt(i) <= '9') i++;
        if (i == digitos || i != n) return null;
        try {
            long v = Long.parseLong(s.substring(inicio));
            if (v < Integer.MIN_VALUE || v > Integer.MAX_VALUE) return null;
            return (int) v;
        } catch (NumberFormatException e) {
            return null;
        }
    }
}
