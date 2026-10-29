package br.gov.dashboard.opcoes;

import br.gov.dashboard.comum.ApiException;
import br.gov.dashboard.comum.Parametros;
import br.gov.dashboard.consulta.ConsultaService;
import br.gov.dashboard.modelo.Indicador;
import br.gov.dashboard.modelo.Modelo;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonPropertyOrder;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;
import java.util.Map;

/** GET /api/opcoes — anos e UFs realmente disponíveis. */
@RestController
public class OpcoesController {

    private final Modelo modelo;
    private final ConsultaService service;

    public OpcoesController(Modelo modelo, ConsultaService service) {
        this.modelo = modelo;
        this.service = service;
    }

    @RequestMapping("/api/opcoes")
    public OpcoesResponse opcoes(@RequestParam Map<String, String> p) {
        if (modelo.vazio()) {
            throw new ApiException(503, "não há dado processado: rode ./dashboard --atualizar ibge");
        }
        Indicador ind = Parametros.indicador(p); // validado; a lista não depende dele
        String uf = p.get("uf");
        if (uf == null) uf = "";

        if (!uf.isEmpty() && modelo.ufDeSigla(uf) < modelo.nUfs()) {
            String sigla = modelo.uf(modelo.ufDeSigla(uf)).sigla();
            return new OpcoesResponse(service.anos(modelo), List.of(sigla), uf);
        }
        return new OpcoesResponse(service.anos(modelo), service.siglasUf(modelo), null);
    }

    @JsonInclude(JsonInclude.Include.NON_NULL)
    @JsonPropertyOrder({"anos", "ufs", "uf"})
    public record OpcoesResponse(List<Integer> anos, List<String> ufs, String uf) {
    }
}
