package br.gov.dashboard.comum;

import com.fasterxml.jackson.annotation.JsonPropertyOrder;

/** Envelope de erro: {@code {"erro":{"codigo":N,"mensagem":"…"}}}. */
@JsonPropertyOrder({"erro"})
public record ErroEnvelope(Erro erro) {

    @JsonPropertyOrder({"codigo", "mensagem"})
    public record Erro(int codigo, String mensagem) {
    }

    public static ErroEnvelope de(int status, String mensagem) {
        return new ErroEnvelope(new Erro(status, mensagem));
    }
}
