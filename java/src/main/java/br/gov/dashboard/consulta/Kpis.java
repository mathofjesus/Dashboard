package br.gov.dashboard.consulta;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonPropertyOrder;

/** KPIs sobre o conjunto inteiro do filtro, antes de {@code limite}. */
@JsonPropertyOrder({"min", "max", "media", "soma", "com_dado"})
public record Kpis(int min, int max, int media, long soma, @JsonProperty("com_dado") int comDado) {
}
