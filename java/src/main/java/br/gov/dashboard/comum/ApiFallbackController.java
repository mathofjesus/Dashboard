package br.gov.dashboard.comum;

import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

/** Fallback de /api/*: rota desconhecida responde o envelope, nunca o do Spring. */
@RestController
public class ApiFallbackController {

    @RequestMapping("/api/**")
    public ResponseEntity<ErroEnvelope> naoEncontrada() {
        return ResponseEntity.status(404).body(ErroEnvelope.de(404, "rota não encontrada"));
    }
}
