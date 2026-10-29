package br.gov.dashboard.consulta;

import br.gov.dashboard.comum.ApiException;
import br.gov.dashboard.comum.Parametros;
import br.gov.dashboard.comum.Texto;
import br.gov.dashboard.modelo.Indicador;
import br.gov.dashboard.modelo.Modelo;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** GET /api/consulta — a tabela, os KPIs e a série do recorte. */
@RestController
public class ConsultaController {

    private final Modelo modelo;
    private final ConsultaService service;

    public ConsultaController(Modelo modelo, ConsultaService service) {
        this.modelo = modelo;
        this.service = service;
    }

    @RequestMapping("/api/consulta")
    public ConsultaResponse consulta(@RequestParam Map<String, String> p) {
        if (modelo.vazio()) {
            throw new ApiException(503, "não há dado processado: rode ./dashboard --atualizar ibge");
        }

        Indicador ind = Parametros.indicador(p);
        boolean nivelUf = Parametros.nivelUf(p);
        int ano = Parametros.ano(p);
        int limite = Parametros.limite(p);

        String uf = nuloParaVazio(p.get("uf"));
        String cod = nuloParaVazio(p.get("cod"));
        String busca = Texto.vazioParaNulo(p.get("busca"));
        String ordenar = Texto.vazioParaNulo(p.get("ordenar"));
        String fonte = Texto.vazioParaNulo(p.get("fonte"));

        if (nivelUf && !cod.isEmpty()) {
            throw new ApiException(400,
                    "nível UF não aceita o filtro cod=: a linha já é a UF");
        }

        Resultado r = service.consultar(modelo, ind, nivelUf, uf, ano, busca, ordenar, limite);

        List<Integer> serie = null;
        if (!cod.isEmpty()) {
            serie = service.serieMunicipio(modelo, ind, cod);
            if (serie == null) {
                throw new ApiException(400, "município " + cod + " não existe na base");
            }
        } else if (nivelUf && !uf.isEmpty() && modelo.ufDeSigla(uf) < modelo.nUfs()) {
            serie = service.serieUf(modelo, ind, uf);
            if (serie == null) {
                throw new ApiException(400, "não consegui montar a série da UF " + uf);
            }
        }

        List<ConsultaResponse.Registro> registros = new ArrayList<>(r.linhas().size());
        for (Linha l : r.linhas()) {
            registros.add(new ConsultaResponse.Registro(
                    l.cod(), l.nome(), l.valor() == Modelo.AUSENTE ? null : l.valor()));
        }

        ConsultaResponse.Consulta eco = new ConsultaResponse.Consulta(
                fonte == null ? "ibge" : fonte,
                ind.nome(),
                nivelUf ? "uf" : "municipio",
                uf,
                ano,
                busca,
                ordenar,
                limite > 0 ? limite : null,
                cod.isEmpty() ? null : cod);

        return new ConsultaResponse(eco, r.total(), r.kpis(), registros, serie,
                serie == null ? null : service.anos(modelo));
    }

    private static String nuloParaVazio(String s) {
        return s == null ? "" : s;
    }
}
