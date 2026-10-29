package br.gov.dashboard.modelo;

import java.util.Arrays;

/** Fixture de três municípios e três anos, igual a tests/fixture_model.h. */
public final class Modelos {

    private Modelos() {
    }

    public static Modelo fixture() {
        Localidade[] locais = {
                new Localidade("1100015", "Porto Velho", "RO", "Norte"),
                new Localidade("1200401", "Rio Branco", "AC", "Norte"),
                new Localidade("4106902", "Curitiba", "PR", "Sul")
        };
        Uf[] ufs = {
                new Uf("AC", "Acre", "Norte"),
                new Uf("PR", "Paraná", "Sul"),
                new Uf("RO", "Rondônia", "Norte")
        };
        int[] anos = {2019, 2021, 2024};
        int[][] pop = {
                {1000, 1200, 1500},
                {800, -1, 900},
                {2000, 2100, 2200}
        };
        int[] valores = new int[Indicador.TOTAL * locais.length * anos.length];
        Arrays.fill(valores, Modelo.AUSENTE);
        int passo = locais.length * anos.length;
        for (int i = 0; i < locais.length; i++) {
            for (int j = 0; j < anos.length; j++) {
                valores[Indicador.POPULACAO.codigo() * passo + i * anos.length + j] = pop[i][j];
            }
        }
        return new Modelo(locais, ufs, anos, valores);
    }
}
