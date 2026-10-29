package br.gov.dashboard.ingestao.saude;

import br.gov.dashboard.ingestao.comum.ArquivoZip;
import br.gov.dashboard.ingestao.comum.Csv;
import br.gov.dashboard.ingestao.comum.IngestaoException;

import java.io.BufferedWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Locale;

/**
 * Poda do CNES para JSON Lines, o porte de {@code saude_processa_cnes}.
 *
 * O ZIP inteiro é descomprimido em memória (o real tem 231 MB), e só as seis
 * colunas que o dashboard usa sobrevivem. As colunas são localizadas por nome,
 * nunca por posição — trocar latitude por longitude em silêncio seria o pior
 * modo de falha possível.
 */
public final class ProcessadorCnes {

    public static final List<String> COLUNAS = List.of(
            "CO_IBGE", "NO_RAZAO_SOCIAL", "NO_FANTASIA",
            "DS_NIVEL_HIERARQUIA", "NU_LATITUDE", "NU_LONGITUDE");

    private static final List<String> CHAVES = List.of(
            "codarea", "razao", "fantasia", "nivel", "lat", "lon");

    private static final int CAMPO_MAX = 511; // char campo[512] do C

    private ProcessadorCnes() {
    }

    public static void executar(Path zipLocal, Path saida, long limiteAmostra) {
        if (zipLocal == null || saida == null) {
            throw new IngestaoException("saude_processa_cnes: caminho do ZIP ou da saída é vazio");
        }

        String nomeCsv;
        List<String> entradas;
        try {
            entradas = ArquivoZip.lista(zipLocal);
            nomeCsv = ArquivoZip.primeiraCsv(zipLocal);
        } catch (IOException e) {
            throw new IngestaoException(
                    "saude_processa_cnes: não consegui abrir o ZIP " + zipLocal, e);
        }
        if (nomeCsv == null) {
            throw new IngestaoException(
                    "saude_processa_cnes: nenhum .csv dentro de " + zipLocal);
        }

        byte[] bruto;
        try {
            bruto = ArquivoZip.extrai(zipLocal, nomeCsv);
        } catch (IOException e) {
            throw new IngestaoException(
                    "saude_processa_cnes: não consegui extrair " + nomeCsv + " de " + zipLocal, e);
        }
        if (bruto.length == 0) {
            throw new IngestaoException("saude_processa_cnes: o CSV de dentro de "
                    + zipLocal + " saiu vazio");
        }
        System.err.printf("    %s: %d bytes, %d entradas%n", nomeCsv, bruto.length, entradas.size());

        Csv csv = Csv.abrir(bruto, Csv.detectaDelimitador(bruto), StandardCharsets.ISO_8859_1);
        if (csv == null) {
            throw new IngestaoException("saude_processa_cnes: não consegui ler o CSV");
        }

        int[] idx = new int[COLUNAS.size()];
        for (int i = 0; i < COLUNAS.size(); i++) {
            int pos = csv.indiceColuna(COLUNAS.get(i));
            if (pos < 0) {
                throw new IngestaoException(
                        "saude_processa_cnes: o CSV não tem a coluna " + COLUNAS.get(i));
            }
            idx[i] = pos;
        }

        int total = csv.numLinhas();
        long alvo = (limiteAmostra > 0 && limiteAmostra < total)
                ? limiteAmostra : (total > 0 ? total - 1L : 0L);

        Path pai = saida.toAbsolutePath().getParent();
        try {
            if (pai != null) Files.createDirectories(pai);
            try (BufferedWriter out = Files.newBufferedWriter(saida, StandardCharsets.UTF_8)) {
                escreveCabecalho(out, total > 0 ? total - 1 : 0);
                long escritas = 0;
                for (int i = 1; i <= alvo; i++) {
                    out.write(linha(csv, i, idx));
                    out.write('\n');
                    escritas++;
                }
                out.flush();
                System.err.printf("    %d estabelecimentos gravados em %s%n", escritas, saida);
            }
        } catch (IOException e) {
            throw new IngestaoException(
                    "saude_processa_cnes: não consegui abrir " + saida + " para escrita", e);
        }
    }

    private static void escreveCabecalho(BufferedWriter out, int linhas) throws IOException {
        out.write("{\"formato\":\"cnes\",\"linhas\":");
        out.write(Integer.toString(linhas));
        out.write(",\"colunas\":[");
        for (int i = 0; i < COLUNAS.size(); i++) {
            if (i > 0) out.write(',');
            out.write('"');
            out.write(COLUNAS.get(i));
            out.write('"');
        }
        out.write("],\"chaves\":[");
        for (int i = 0; i < CHAVES.size(); i++) {
            if (i > 0) out.write(',');
            out.write('"');
            out.write(CHAVES.get(i));
            out.write('"');
        }
        out.write("]}\n");
    }

    private static String linha(Csv csv, int i, int[] idx) {
        StringBuilder sb = new StringBuilder(256);

        String brutoCod = csv.campo(i, idx[0], CAMPO_MAX);
        String cod = codigoOuAusente(brutoCod);
        if (cod != null) {
            sb.append("{\"codarea\":\"");
            sb.append(escapa(cod));
            sb.append("\",");
        } else {
            sb.append("{\"codarea\":-1,\"codigo_bruto\":");
            sb.append(jsonString(brutoCod));
            sb.append(',');
        }

        sb.append("\"razao\":");
        sb.append(jsonString(csv.campo(i, idx[1], CAMPO_MAX)));
        sb.append(",\"fantasia\":");
        sb.append(jsonString(csv.campo(i, idx[2], CAMPO_MAX)));
        sb.append(",\"nivel\":");
        sb.append(jsonString(csv.campo(i, idx[3], CAMPO_MAX)));

        String lat = csv.campo(i, idx[4], CAMPO_MAX);
        sb.append(",\"lat\":");
        if (ausente(lat)) {
            sb.append("null");
        } else {
            sb.append(String.format(Locale.ROOT, "%.6f", numero(lat)));
        }

        String lon = csv.campo(i, idx[5], CAMPO_MAX);
        sb.append(",\"lon\":");
        if (ausente(lon)) {
            sb.append("null");
        } else {
            sb.append(String.format(Locale.ROOT, "%.6f", numero(lon)));
        }
        sb.append('}');
        return sb.toString();
    }

    private static String codigoOuAusente(String bruto) {
        if (bruto == null) return null;
        int k = 0;
        while (k < bruto.length() && (bruto.charAt(k) == ' ' || bruto.charAt(k) == '\t')) k++;
        String s = bruto.substring(k);
        if (s.isEmpty() || s.equals("..") || s.equals("...")) return null;
        return s;
    }

    private static boolean ausente(String campo) {
        int k = 0;
        while (k < campo.length() && campo.charAt(k) == ' ') k++;
        String s = campo.substring(k);
        return s.isEmpty() || s.equals("..");
    }

    /** strtod(p, NULL): lê o maior prefixo numérico; 0.0 quando não há. */
    static double numero(String bruto) {
        int k = 0;
        while (k < bruto.length() && bruto.charAt(k) == ' ') k++;
        String s = bruto.substring(k);
        int fim = 0;
        if (fim < s.length() && (s.charAt(fim) == '+' || s.charAt(fim) == '-')) fim++;
        int digitos = 0;
        while (fim < s.length() && s.charAt(fim) >= '0' && s.charAt(fim) <= '9') {
            fim++;
            digitos++;
        }
        if (fim < s.length() && s.charAt(fim) == '.') {
            fim++;
            while (fim < s.length() && s.charAt(fim) >= '0' && s.charAt(fim) <= '9') {
                fim++;
                digitos++;
            }
        }
        if (digitos == 0) return 0.0;
        if (fim < s.length() && (s.charAt(fim) == 'e' || s.charAt(fim) == 'E')) {
            int e = fim + 1;
            if (e < s.length() && (s.charAt(e) == '+' || s.charAt(e) == '-')) e++;
            int d = e;
            while (e < s.length() && s.charAt(e) >= '0' && s.charAt(e) <= '9') e++;
            if (e > d) fim = e;
        }
        try {
            return Double.parseDouble(s.substring(0, fim));
        } catch (NumberFormatException ex) {
            return 0.0;
        }
    }

    private static String jsonString(String s) {
        return "\"" + escapa(s == null ? "" : s) + "\"";
    }

    /** O mesmo escape de {@code text_json_escape}, inclusive \\u00xx minúsculo. */
    static String escapa(String s) {
        StringBuilder sb = new StringBuilder(s.length() + 8);
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"' -> sb.append("\\\"");
                case '\\' -> sb.append("\\\\");
                case '\n' -> sb.append("\\n");
                case '\r' -> sb.append("\\r");
                case '\t' -> sb.append("\\t");
                case '\b' -> sb.append("\\b");
                case '\f' -> sb.append("\\f");
                default -> {
                    if (c < 0x20) {
                        sb.append("\\u");
                        String hex = Integer.toHexString(c);
                        for (int k = hex.length(); k < 4; k++) sb.append('0');
                        sb.append(hex);
                    } else {
                        sb.append(c);
                    }
                }
            }
        }
        return sb.toString();
    }
}
