package br.gov.dashboard.valores;

import br.gov.dashboard.comum.ApiException;
import br.gov.dashboard.comum.Parametros;
import br.gov.dashboard.consulta.ConsultaService;
import br.gov.dashboard.consulta.Linha;
import br.gov.dashboard.consulta.Resultado;
import br.gov.dashboard.modelo.Indicador;
import br.gov.dashboard.modelo.Modelo;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonPropertyOrder;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * GET /api/valores — {@code codarea → valor} do recorte inteiro, sem paginação.
 *
 * Existe separado de /api/consulta porque o mapa precisa de todas as
 * localidades; paginar o coroplético pintaria de "sem dado" quem só não coube
 * na página.
 */
@RestController
public class ValoresController {

    private final Modelo modelo;
    private final ConsultaService service;

    public ValoresController(Modelo modelo, ConsultaService service) {
        this.modelo = modelo;
        this.service = service;
    }

    @RequestMapping("/api/valores")
    public ValoresResponse valores(@RequestParam Map<String, String> p) {
        if (modelo.vazio()) {
            throw new ApiException(503, "não há dado processado: rode ./dashboard --atualizar ibge");
        }

        Indicador ind = Parametros.indicador(p);
        int ano = Parametros.ano(p);
        boolean nivelUf = Parametros.nivelUf(p);
        boolean querNomes = Parametros.nomes(p);

        String uf = p.get("uf");
        if (uf == null) uf = "";

        Resultado r = service.consultar(modelo, ind, nivelUf, uf, ano, null, null, 0);

        Map<String, Integer> valores = new LinkedHashMap<>();
        Map<String, String> nomes = querNomes ? new LinkedHashMap<>() : null;
        for (Linha l : r.linhas()) {
            valores.put(l.cod(), l.valor() == Modelo.AUSENTE ? null : l.valor());
            if (nomes != null) nomes.put(l.cod(), l.nome());
        }

        return new ValoresResponse(r.total(), r.kpis().comDado(), valores, nomes);
    }

    @JsonInclude(JsonInclude.Include.NON_NULL)
    @JsonPropertyOrder({"total", "com_dado", "valores", "nomes"})
    public record ValoresResponse(
            int total,
            @JsonProperty("com_dado") int comDado,
            Map<String, Integer> valores,
            Map<String, String> nomes) {
    }
}
