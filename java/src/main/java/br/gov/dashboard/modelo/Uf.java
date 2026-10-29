package br.gov.dashboard.modelo;

/** UF do IBGE, como no {@code uf_info} do C. */
public record Uf(String sigla, String nome, String regiao) {
}
