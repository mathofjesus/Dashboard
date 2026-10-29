package br.gov.dashboard.geografia;

import br.gov.dashboard.comum.ApiException;
import br.gov.dashboard.comum.Texto;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.util.Map;

/**
 * As três rotas de geometria. Elas não dependem do modelo: a malha é desenho,
 * não dado, e continua respondendo mesmo sem índice de população.
 */
@RestController
public class GeoController {

    private static final MediaType JSON_UTF8 = MediaType.parseMediaType("application/json;charset=UTF-8");

    private final GeoJsonService service;

    public GeoController(GeoJsonService service) {
        this.service = service;
    }

    @RequestMapping("/api/geo/municipios")
    public ResponseEntity<byte[]> municipios(@RequestParam Map<String, String> p) {
        return responde(GeoNivel.MUNICIPIOS, p.get("uf"));
    }

    @RequestMapping("/api/geo/estados")
    public ResponseEntity<byte[]> estados(@RequestParam Map<String, String> p) {
        return responde(GeoNivel.ESTADOS, p.get("uf"));
    }

    @RequestMapping("/api/mapa/uf")
    public ResponseEntity<byte[]> mapaUf(@RequestParam Map<String, String> p) {
        String nivel = p.get("nivel");
        GeoNivel alvo = (nivel != null && (Texto.igualIgnorandoCaixa(nivel, "municipio")
                || Texto.igualIgnorandoCaixa(nivel, "municipios")))
                ? GeoNivel.MUNICIPIOS
                : GeoNivel.ESTADOS;
        return responde(alvo, p.get("uf"));
    }

    private ResponseEntity<byte[]> responde(GeoNivel nivel, String ufBruta) {
        String uf = ufBruta == null ? "" : ufBruta;
        String codigo = null;
        if (!uf.isEmpty()) {
            codigo = UfCodigo.codigo(uf);
            if (codigo == null) throw new ApiException(400, "UF inválida");
        }

        byte[] corpo = codigo == null ? service.bruto(nivel) : service.filtrado(nivel, codigo);
        if (corpo == null) {
            throw new ApiException(503, "Execute: ./dashboard --atualizar");
        }
        return ResponseEntity.ok().contentType(JSON_UTF8).body(corpo);
    }
}
