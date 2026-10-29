package br.gov.dashboard.modelo;

/** Município do IBGE, como no {@code locality} do C. */
public record Localidade(String cod, String nome, String uf, String regiao) {
}
