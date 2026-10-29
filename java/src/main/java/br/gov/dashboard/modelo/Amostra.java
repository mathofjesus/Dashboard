package br.gov.dashboard.modelo;

import java.io.IOException;
import java.nio.file.Path;

/** Amostra determinística do {@code --amostra} (model.c:835). */
public final class Amostra {

    private static final Localidade[] LOCAIS = {
            new Localidade("1100015", "Alta Floresta D'Oeste", "RO", "Norte"),
            new Localidade("1200401", "Rio Branco", "AC", "Norte"),
            new Localidade("4106902", "Curitiba", "PR", "Sul")
    };

    private static final Uf[] UFS = {
            new Uf("AC", "Acre", "Norte"),
            new Uf("PR", "Paraná", "Sul"),
            new Uf("RO", "Rondônia", "Norte")
    };

    private static final int[] ANOS = {2019, 2021, 2024};

    private static final int[][] POPULACAO = {
            {30014, 30995, 22853},
            {114382, 142012, 145035},
            {1853097, 1950764, 1969868}
    };

    private Amostra() {
    }

    public static Modelo modelo() {
        int nLocal = LOCAIS.length;
        int nAnos = ANOS.length;
        int[] valores = new int[Indicador.TOTAL * nLocal * nAnos];
        java.util.Arrays.fill(valores, Modelo.AUSENTE);
        int passo = nLocal * nAnos;
        for (int i = 0; i < nLocal; i++) {
            for (int j = 0; j < nAnos; j++) {
                valores[Indicador.POPULACAO.codigo() * passo + i * nAnos + j] = POPULACAO[i][j];
            }
        }
        return new Modelo(LOCAIS, UFS, ANOS, valores);
    }

    public static void gravar(Path caminho) throws IOException {
        EscritorIndice.gravar(modelo(), caminho);
    }
}
