package br.gov.dashboard.consulta;

import java.util.List;

/** Resultado interno: as linhas já limitadas, os KPIs e o total antes do limite. */
public record Resultado(List<Linha> linhas, Kpis kpis, int total) {
}
