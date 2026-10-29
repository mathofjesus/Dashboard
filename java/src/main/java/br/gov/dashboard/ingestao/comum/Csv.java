package br.gov.dashboard.ingestao.comum;

import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * CSV o mais próximo possível do {@code csv.c}: detecção de delimitador,
 * aspas com {@code ""}, BOM e codificação declarada por dataset.
 *
 * Não é um parser RFC 4180 completo — e não deve ser: campos com quebra de
 * linha dentro de aspas não existem nos arquivos públicos que o dashboard lê,
 * e o C os trata como duas linhas. Fazer diferente aqui mudaria a saída sem
 * mudar o dado.
 */
public final class Csv {

    private final List<List<String>> linhas;

    private Csv(List<List<String>> linhas) {
        this.linhas = linhas;
    }

    public int numLinhas() {
        return linhas.size();
    }

    public List<String> linha(int i) {
        return linhas.get(i);
    }

    public String campo(int linha, int coluna) {
        if (linha < 0 || linha >= linhas.size()) return "";
        List<String> campos = linhas.get(linha);
        if (coluna < 0 || coluna >= campos.size()) return "";
        String v = campos.get(coluna);
        return v == null ? "" : v;
    }

    /** Valor truncado como o {@code char[512]} do C, sem cortar no meio de nada. */
    public String campo(int linha, int coluna, int maxChars) {
        String v = campo(linha, coluna);
        return v.length() <= maxChars ? v : v.substring(0, maxChars);
    }

    public int indiceColuna(String nome) {
        if (nome == null || linhas.isEmpty()) return -1;
        List<String> cab = linhas.get(0);
        for (int i = 0; i < cab.size(); i++) {
            if (nome.equals(cab.get(i))) return i;
        }
        return -1;
    }

    public String cabecalho(int coluna) {
        return campo(0, coluna);
    }

    /**
     * Detecta ';' ou ',' contando ocorrências fora de aspas na primeira linha.
     * Empate vai para ';', como no C.
     */
    public static char detectaDelimitador(byte[] buf) {
        if (buf == null || buf.length == 0) return ',';
        int i = 0;
        if (buf.length >= 3 && (buf[0] & 0xff) == 0xEF
                && (buf[1] & 0xff) == 0xBB && (buf[2] & 0xff) == 0xBF) {
            i = 3;
        }
        int fim = i;
        while (fim < buf.length && buf[fim] != '\n' && buf[fim] != '\r') fim++;

        int sem = 0, virg = 0;
        boolean aspas = false;
        for (int j = i; j < fim; j++) {
            char c = (char) (buf[j] & 0xff);
            if (c == '"') {
                if (j + 1 < fim && buf[j + 1] == '"') {
                    j++;
                } else {
                    aspas = !aspas;
                }
            } else if (!aspas) {
                if (c == ';') sem++;
                if (c == ',') virg++;
            }
        }
        if (sem > virg) return ';';
        if (virg > sem) return ',';
        return ';';
    }

    public static Csv abrir(byte[] bytes, char delimitador, Charset codificacao) {
        if (bytes == null) return null;
        String texto = new String(bytes, codificacao);
        // O C pula o BOM depois da conversão: em UTF-8 ele é 0xEF 0xBB 0xBF;
        // em Latin-1 os mesmos bytes viram "ï»¿" e NÃO são BOM.
        if (StandardCharsets.UTF_8.equals(codificacao) && texto.startsWith("\uFEFF")) {
            texto = texto.substring(1);
        }

        List<String> brutas = new ArrayList<>();
        int len = texto.length();
        int start = 0;
        while (start < len) {
            int nl = start;
            while (nl < len && texto.charAt(nl) != '\n' && texto.charAt(nl) != '\r') nl++;
            brutas.add(texto.substring(start, nl));
            if (nl >= len) break;
            if (texto.charAt(nl) == '\r' && nl + 1 < len && texto.charAt(nl + 1) == '\n') {
                start = nl + 2;
            } else {
                start = nl + 1;
            }
        }

        List<List<String>> linhas = new ArrayList<>(brutas.size());
        for (String linha : brutas) {
            linhas.add(campos(linha, delimitador));
        }
        return new Csv(Collections.unmodifiableList(linhas));
    }

    private static List<String> campos(String linha, char delimitador) {
        int len = linha.length();
        int ncampos = 1;
        boolean aspas = false;
        for (int i = 0; i < len; i++) {
            char c = linha.charAt(i);
            if (c == '"') {
                if (i + 1 < len && linha.charAt(i + 1) == '"') {
                    i++;
                } else {
                    aspas = !aspas;
                }
            } else if (!aspas && c == delimitador) {
                ncampos++;
            }
        }

        List<String> campos = new ArrayList<>(ncampos);
        aspas = false;
        int inicioCampo = 0;
        for (int i = 0; i <= len; i++) {
            boolean fim = false;
            if (i == len) {
                fim = true;
            } else {
                char ch = linha.charAt(i);
                if (ch == '"') {
                    if (i + 1 < len && linha.charAt(i + 1) == '"') {
                        i++;
                    } else {
                        aspas = !aspas;
                    }
                } else if (!aspas && ch == delimitador) {
                    fim = true;
                }
            }
            if (fim) {
                campos.add(processaCampo(linha.substring(inicioCampo, i)));
                inicioCampo = i + 1;
                if (i == len) break;
            }
        }
        return campos;
    }

    private static String processaCampo(String bruto) {
        int n = bruto.length();
        if (n >= 2 && bruto.charAt(0) == '"' && bruto.charAt(n - 1) == '"') {
            String interior = bruto.substring(1, n - 1);
            StringBuilder sb = new StringBuilder(interior.length());
            for (int i = 0; i < interior.length(); i++) {
                char c = interior.charAt(i);
                if (c == '"' && i + 1 < interior.length()
                        && interior.charAt(i + 1) == '"') {
                    sb.append('"');
                    i++;
                } else {
                    sb.append(c);
                }
            }
            return sb.toString();
        }
        return bruto;
    }
}
