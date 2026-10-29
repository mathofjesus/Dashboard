package br.gov.dashboard.comum;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.ResponseEntity;
import org.springframework.web.HttpRequestMethodNotSupportedException;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;
import org.springframework.web.servlet.resource.NoResourceFoundException;

/**
 * Substitui o ProblemDetail padrão do Spring pelo envelope do dashboard.
 *
 * O front lê {@code corpo.erro.mensagem} (web/api.js). Deixar o formato do
 * Spring vazar transformaria "ano 2022 inexistente" em um documento com
 * "detail"/"title", e a tela mostraria um erro genérico.
 */
@RestControllerAdvice
public class ErroHandler {

    private static final Logger log = LoggerFactory.getLogger(ErroHandler.class);

    @ExceptionHandler(ApiException.class)
    public ResponseEntity<ErroEnvelope> api(ApiException e) {
        return ResponseEntity.status(e.status())
                .body(ErroEnvelope.de(e.status(), e.getMessage()));
    }

    @ExceptionHandler(NoResourceFoundException.class)
    public ResponseEntity<Void> arquivoNaoEncontrado(NoResourceFoundException e) {
        return ResponseEntity.notFound().build();
    }

    @ExceptionHandler(HttpRequestMethodNotSupportedException.class)
    public ResponseEntity<ErroEnvelope> metodoNaoPermitido(HttpRequestMethodNotSupportedException e) {
        return ResponseEntity.status(405).body(ErroEnvelope.de(405, "método não permitido"));
    }

    @ExceptionHandler(Exception.class)
    public ResponseEntity<ErroEnvelope> inesperado(Exception e) {
        log.error("erro inesperado ao atender requisição", e);
        return ResponseEntity.status(500).body(ErroEnvelope.de(500, "erro interno"));
    }
}
