package br.gov.dashboard.modelo;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/**
 * Escritor do {@code modelo.idx}, com o mesmo layout do C e troca atômica por
 * rename: um servidor no ar nunca lê um índice pela metade.
 */
public final class EscritorIndice {

    private EscritorIndice() {
    }

    public static void gravar(Modelo m, Path caminho) throws IOException {
        int nAnos = m.nAnos();
        int nLocal = m.nLocal();
        int nUfs = m.nUfs();
        int totalValores = Indicador.TOTAL * nLocal * nAnos;

        long capacidade = LeitorIndice.MAGIC.length()
                + 4L * 5
                + 4L * nAnos
                + (long) nUfs * (LeitorIndice.TAM_SIGLA + LeitorIndice.TAM_NOME_UF + LeitorIndice.TAM_REGIAO)
                + (long) nLocal * (LeitorIndice.TAM_COD + LeitorIndice.TAM_NOME_LOCAL
                        + LeitorIndice.TAM_SIGLA + LeitorIndice.TAM_REGIAO)
                + 4L * totalValores;
        if (capacidade > Integer.MAX_VALUE - 8) throw new IOException("índice grande demais para a JVM");

        ByteBuffer b = ByteBuffer.allocate((int) capacidade).order(ByteOrder.LITTLE_ENDIAN);
        b.put(LeitorIndice.MAGIC.getBytes(StandardCharsets.ISO_8859_1));
        b.putInt(LeitorIndice.VERSAO);
        b.putInt(nAnos);
        b.putInt(nLocal);
        b.putInt(nUfs);
        b.putInt(0);

        for (int i = 0; i < nAnos; i++) b.putInt(m.ano(i));

        for (int i = 0; i < nUfs; i++) {
            Uf u = m.uf(i);
            texto(b, u.sigla(), LeitorIndice.TAM_SIGLA);
            texto(b, u.nome(), LeitorIndice.TAM_NOME_UF);
            texto(b, u.regiao(), LeitorIndice.TAM_REGIAO);
        }

        for (int i = 0; i < nLocal; i++) {
            Localidade l = m.local(i);
            texto(b, l.cod(), LeitorIndice.TAM_COD);
            texto(b, l.nome(), LeitorIndice.TAM_NOME_LOCAL);
            texto(b, l.uf(), LeitorIndice.TAM_SIGLA);
            texto(b, l.regiao(), LeitorIndice.TAM_REGIAO);
        }

        int[] valores = m.valoresCrus();
        if (valores.length != totalValores) throw new IOException("tamanho de valores inconsistente");
        for (int v : valores) b.putInt(v);

        Path pai = caminho.toAbsolutePath().getParent();
        if (pai != null) Files.createDirectories(pai);
        Path tmp = caminho.resolveSibling(caminho.getFileName() + ".tmp");
        Files.write(tmp, b.array());

        try {
            Files.move(tmp, caminho, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (java.nio.file.AtomicMoveNotSupportedException e) {
            Files.move(tmp, caminho, StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private static void texto(ByteBuffer b, String s, int n) {
        byte[] raw = (s == null ? "" : s).getBytes(StandardCharsets.UTF_8);
        int k = Math.min(raw.length, n - 1);
        b.put(raw, 0, k);
        for (int i = k; i < n; i++) b.put((byte) 0);
    }
}
