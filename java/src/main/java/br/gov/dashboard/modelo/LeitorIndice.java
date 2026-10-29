package br.gov.dashboard.modelo;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

/**
 * Leitor do {@code modelo.idx} (magic DMODIDX1, versão 1).
 *
 * O arquivo é little-endian e usa campos de texto de tamanho fixo, com NUL no
 * fim — o mesmo layout de model.c. Ler o arquivo existente é o que permite
 * rodar C e Java sobre o mesmo dado durante a migração.
 */
public final class LeitorIndice {

    public static final String MAGIC = "DMODIDX1\n";
    public static final int VERSAO = 1;

    public static final int TAM_COD = 8;
    public static final int TAM_NOME_LOCAL = 96;
    public static final int TAM_SIGLA = 3;
    public static final int TAM_NOME_UF = 64;
    public static final int TAM_REGIAO = 24;

    private LeitorIndice() {
    }

    public static Modelo ler(Path caminho) throws IOException {
        byte[] bytes = Files.readAllBytes(caminho);
        ByteBuffer b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);

        if (b.remaining() < MAGIC.length() + 20) throw new IOException("índice truncado");
        byte[] magic = new byte[MAGIC.length()];
        b.get(magic);
        if (!Arrays.equals(magic, MAGIC.getBytes(StandardCharsets.ISO_8859_1))) {
            throw new IOException("magic do índice não confere");
        }

        long versao = Integer.toUnsignedLong(b.getInt());
        if (versao != VERSAO) throw new IOException("versão do índice não suportada: " + versao);

        long nAnos = Integer.toUnsignedLong(b.getInt());
        long nLocal = Integer.toUnsignedLong(b.getInt());
        long nUfs = Integer.toUnsignedLong(b.getInt());
        b.getInt(); // reserva

        if (nAnos == 0 || nLocal == 0 || nAnos > 100_000 || nLocal > 1_000_000) {
            throw new IOException("contagens inválidas no índice");
        }

        long totalValores = (long) Indicador.TOTAL * nLocal * nAnos;
        if (totalValores > Integer.MAX_VALUE - 8) {
            throw new IOException("índice grande demais para a JVM");
        }

        int[] anos = new int[(int) nAnos];
        for (int i = 0; i < anos.length; i++) anos[i] = b.getInt();

        Uf[] ufs = new Uf[(int) nUfs];
        for (int i = 0; i < ufs.length; i++) {
            ufs[i] = new Uf(texto(b, TAM_SIGLA), texto(b, TAM_NOME_UF), texto(b, TAM_REGIAO));
        }

        Localidade[] local = new Localidade[(int) nLocal];
        for (int i = 0; i < local.length; i++) {
            local[i] = new Localidade(
                    texto(b, TAM_COD),
                    texto(b, TAM_NOME_LOCAL),
                    texto(b, TAM_SIGLA),
                    texto(b, TAM_REGIAO));
        }

        if ((long) b.remaining() < totalValores * 4L) throw new IOException("índice truncado nos valores");
        int[] valores = new int[(int) totalValores];
        for (int i = 0; i < valores.length; i++) valores[i] = b.getInt();

        return new Modelo(local, ufs, anos, valores);
    }

    private static String texto(ByteBuffer b, int n) {
        byte[] raw = new byte[n];
        b.get(raw);
        int k = 0;
        while (k < n && raw[k] != 0) k++;
        return new String(raw, 0, k, StandardCharsets.UTF_8);
    }
}
