package br.gov.dashboard.consulta;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonPropertyOrder;

import java.util.List;

/**
 * Envelope de /api/consulta no formato da spec §8:
 * {@code {consulta, total, kpis, registros, serie?, serie_anos?}}.
 *
 * {@code serie} só existe quando há recorte; por isso NON_NULL no envelope.
 * O {@code valor} de um registro é nullable e continua sendo emitido como
 * {@code null} — a anotação não é herdada pelo record interno.
 */
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonPropertyOrder({"consulta", "total", "kpis", "registros", "serie", "serie_anos"})
public record ConsultaResponse(
        Consulta consulta,
        int total,
        Kpis kpis,
        List<Registro> registros,
        List<Integer> serie,
        @JsonProperty("serie_anos") List<Integer> serieAnos) {

    @JsonInclude(JsonInclude.Include.NON_NULL)
    @JsonPropertyOrder({"fonte", "indicador", "nivel", "uf", "ano", "busca", "ordenar", "limite", "cod"})
    public record Consulta(
            String fonte,
            String indicador,
            String nivel,
            String uf,
            int ano,
            String busca,
            String ordenar,
            Integer limite,
            String cod) {
    }

    @JsonPropertyOrder({"cod", "nome", "valor"})
    public record Registro(String cod, String nome, Integer valor) {
    }
}
