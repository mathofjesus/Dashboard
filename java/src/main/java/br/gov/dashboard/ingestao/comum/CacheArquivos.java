package br.gov.dashboard.ingestao.comum;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.FileTime;
import java.time.Instant;
import java.util.Locale;

/**
 * Cache dos downloads brutos em {@code data/cache/}, no mesmo formato do C.
 *
 * O nome do arquivo é a chave com tudo que não for {@code [A-Za-z0-9_-]}
 * trocado por {@code _}. Isso resolve nomes de arquivo a partir de URLs e, de
 * quebra, impede que {@code ..} escape do diretório. O TTL fica num arquivo de
 * lado ({@code .ttl}) porque o conteúdo é o download cru, byte a byte.
 */
public final class CacheArquivos {

    public static final int TTL_NENHUM = -1;
    public static final int TTL_CORROMPIDO = -2;

    private static final int NOME_MAX = 96;
    private static final String SUFIXO_TMP = ".tmp";
    private static final String SUFIXO_TTL = ".ttl";

    private final Path diretorio;

    public CacheArquivos(Path diretorio) {
        this.diretorio = diretorio;
    }

    public Path diretorio() {
        return diretorio;
    }

    /** Caminho da chave, ou null quando a chave é vazia. Não cria nada. */
    public Path caminho(String chave) {
        if (chave == null || chave.isEmpty()) return null;
        return diretorio.resolve(nomeDoArquivo(chave));
    }

    /**
     * 1 se o arquivo existe e está dentro do TTL; 0 se não existe ou expirou.
     * Ausência de .ttl significa imutável; .ttl ilegível significa expirado.
     */
    public boolean tem(String chave) {
        Path p = caminho(chave);
        if (p == null || !Files.isRegularFile(p)) return false;
        long idade = idadeSegundos(p);
        int ttl = ttl(chave);
        return ttl == TTL_NENHUM || (ttl >= 0 && idade <= ttl);
    }

    public byte[] le(String chave) {
        Path p = caminho(chave);
        if (p == null) return null;
        try {
            return Files.readAllBytes(p);
        } catch (IOException e) {
            return null;
        }
    }

    /** Grava os bytes com troca atômica por rename. Devolve false em falha. */
    public boolean grava(String chave, byte[] dados) {
        Path p = caminho(chave);
        if (p == null || dados == null) return false;
        Path tmp = p.resolveSibling(p.getFileName() + SUFIXO_TMP);
        try {
            Files.createDirectories(diretorio);
            Files.write(tmp, dados);
            try {
                Files.move(tmp, p, StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (java.nio.file.AtomicMoveNotSupportedException e) {
                Files.move(tmp, p, StandardCopyOption.REPLACE_EXISTING);
            }
            return true;
        } catch (IOException e) {
            try {
                Files.deleteIfExists(tmp);
            } catch (IOException ignorado) {
                // O .tmp já não existe ou não pode ser removido; o erro que
                // importa é o de gravação, que o chamador transforma em aviso.
            }
            return false;
        }
    }

    public long idadeSegundos(String chave) {
        Path p = caminho(chave);
        if (p == null || !Files.isRegularFile(p)) return -1;
        return idadeSegundos(p);
    }

    private static long idadeSegundos(Path p) {
        try {
            FileTime m = Files.getLastModifiedTime(p);
            long idade = Instant.now().getEpochSecond() - m.toInstant().getEpochSecond();
            return Math.max(0L, idade);
        } catch (IOException e) {
            return -1;
        }
    }

    /**
     * Prazo da chave: {@code -1} sem arquivo de lado, {@code -2} quando o
     * arquivo existe mas não tem um inteiro legível (fail-closed).
     */
    public int ttl(String chave) {
        Path p = caminho(chave);
        if (p == null) return TTL_NENHUM;
        Path ttlPath = p.resolveSibling(p.getFileName() + SUFIXO_TTL);
        if (!Files.isRegularFile(ttlPath)) return TTL_NENHUM;

        try {
            String texto = Files.readString(ttlPath, StandardCharsets.UTF_8);
            int i = 0, n = texto.length();
            while (i < n && ehEspaco(texto.charAt(i))) i++;
            int sinal = 1;
            if (i < n && (texto.charAt(i) == '+' || texto.charAt(i) == '-')) {
                if (texto.charAt(i) == '-') sinal = -1;
                i++;
            }
            int inicioDigitos = i;
            long v = 0;
            while (i < n && texto.charAt(i) >= '0' && texto.charAt(i) <= '9') {
                v = v * 10 + (texto.charAt(i) - '0');
                if (v > Integer.MAX_VALUE) return TTL_CORROMPIDO;
                i++;
            }
            if (i == inicioDigitos) return TTL_CORROMPIDO;
            while (i < n && ehEspaco(texto.charAt(i))) i++;
            if (i != n) return TTL_CORROMPIDO;
            v *= sinal;
            if (v < 0) return TTL_CORROMPIDO;
            return (int) v;
        } catch (IOException e) {
            return TTL_CORROMPIDO;
        }
    }

    public void setTtl(String chave, int segundos) {
        Path p = caminho(chave);
        if (p == null) return;
        Path ttlPath = p.resolveSibling(p.getFileName() + SUFIXO_TTL);
        if (segundos < 0) {
            try {
                Files.deleteIfExists(ttlPath);
            } catch (IOException e) {
                // O prazo antigo continua valendo; não é motivo para abortar
                // uma ingestão inteira.
            }
            return;
        }
        try {
            Files.createDirectories(diretorio);
            Files.writeString(ttlPath, segundos + "\n", StandardCharsets.UTF_8);
        } catch (IOException e) {
            // Mesma regra do C: perder o TTL é mais barato que perder o dado.
        }
    }

    /** Nome de arquivo do C: seguro, com resumo FNV-1a quando longo demais. */
    static String nomeDoArquivo(String chave) {
        // O C percorre bytes, não caracteres: uma chave não-ASCII vira um '_'
        // por byte e o resumo é sobre os bytes UTF-8. Fazer char a char daria
        // outro nome para a mesma chave.
        byte[] bytes = chave.getBytes(StandardCharsets.UTF_8);
        int n = bytes.length;
        StringBuilder nome = new StringBuilder(n);
        for (byte b : bytes) {
            char c = (char) (b & 0xff);
            boolean seguro = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '_' || c == '-';
            nome.append(seguro ? c : '_');
        }
        if (n <= NOME_MAX) return nome.toString();

        int prefixo = NOME_MAX - 17;
        long h = fnv1a(bytes);
        return nome.substring(0, prefixo)
                + String.format(Locale.ROOT, "_%016x", h);
    }

    /** FNV-1a de 64 bits — a mesma constante (não padrão) do cache.c. */
    static long fnv1a(byte[] bytes) {
        long h = 1469598103934665603L;
        for (byte b : bytes) {
            h ^= (b & 0xff);
            h *= 1099511628211L;
        }
        return h;
    }

    private static boolean ehEspaco(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }
}
