package br.gov.dashboard.consulta;

/** Linha da tabela. {@code valor == Modelo.AUSENTE} vira {@code null} no JSON. */
public record Linha(String cod, String nome, int valor) {
}
