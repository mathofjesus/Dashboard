package br.gov.dashboard.consulta;

import br.gov.dashboard.comum.ApiException;
import br.gov.dashboard.modelo.Indicador;
import br.gov.dashboard.modelo.Modelo;
import br.gov.dashboard.modelo.Modelos;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

class ConsultaServiceTest {

    private final ConsultaService service = new ConsultaService();
    private final Modelo m = Modelos.fixture();

    @Test
    void ordemPadraoEhValorDescComAusentePorUltimo() {
        Resultado r = service.consultar(m, Indicador.POPULACAO, false, null, 2021, null, null, 0);

        assertEquals(3, r.total());
        assertEquals(List.of("4106902", "1100015", "1200401"),
                r.linhas().stream().map(Linha::cod).toList());
        assertEquals(Modelo.AUSENTE, r.linhas().get(2).valor());
        assertEquals(1200, r.kpis().min());
        assertEquals(2100, r.kpis().max());
        assertEquals(1650, r.kpis().media());
        assertEquals(3300, r.kpis().soma());
        assertEquals(2, r.kpis().comDado());
    }

    @Test
    void limiteCortaTabelaMasNaoKpis() {
        Resultado r = service.consultar(m, Indicador.POPULACAO, false, null, 2021, null, null, 1);
        assertEquals(1, r.linhas().size());
        assertEquals(3, r.total());
        assertEquals(3300, r.kpis().soma());
    }

    @Test
    void ordenaPorValorAscENomeAsc() {
        Resultado asc = service.consultar(m, Indicador.POPULACAO, false, null, 2021, null, "valor_asc", 0);
        assertEquals(List.of("1100015", "4106902", "1200401"),
                asc.linhas().stream().map(Linha::cod).toList());

        Resultado nome = service.consultar(m, Indicador.POPULACAO, false, null, 2021, null, "nome_asc", 0);
        assertEquals(List.of("4106902", "1100015", "1200401"),
                nome.linhas().stream().map(Linha::cod).toList());
    }

    @Test
    void buscaEhCaseInsensitiveEmNomeECodigo() {
        assertEquals(1, service.consultar(m, Indicador.POPULACAO, false, null, 2021, "rio", null, 0).total());
        assertEquals(1, service.consultar(m, Indicador.POPULACAO, false, null, 2021, "RIO", null, 0).total());
        assertEquals(1, service.consultar(m, Indicador.POPULACAO, false, null, 2021, "1100", null, 0).total());
        assertEquals(0, service.consultar(m, Indicador.POPULACAO, false, null, 2021, "zzz", null, 0).total());
    }

    @Test
    void filtroDeUfAceitaSiglaCodigoENome() {
        assertEquals(1, service.consultar(m, Indicador.POPULACAO, false, "RO", 2021, null, null, 0).total());
        assertEquals(1, service.consultar(m, Indicador.POPULACAO, false, "11", 2021, null, null, 0).total());
        // Paridade com uf_casa (aggregate.c): no filtro de MUNICÍPIO, nome por
        // extenso compara o nome da UF resolvida e ignora a localidade — por
        // isso "Rondônia" casa com todos. O contrato atual é este; corrigir é
        // uma decisão posterior, nos dois lados.
        assertEquals(3, service.consultar(m, Indicador.POPULACAO, false, "Rondônia", 2021, null, null, 0).total());
        assertEquals(0, service.consultar(m, Indicador.POPULACAO, false, "SP", 2021, null, null, 0).total());
    }

    @Test
    void nivelUfSomaOsMunicipiosESeriePorUf() {
        Resultado r = service.consultar(m, Indicador.POPULACAO, true, null, 2021, null, null, 0);
        assertEquals(3, r.total());
        assertEquals(List.of("41", "11", "12"),
                r.linhas().stream().map(Linha::cod).toList());
        assertEquals(Modelo.AUSENTE, r.linhas().get(2).valor(), "AC não tem valor em 2021");

        Resultado soRo = service.consultar(m, Indicador.POPULACAO, true, "RO", 2021, null, null, 0);
        assertEquals(1, soRo.total());
        assertEquals(1200, soRo.linhas().get(0).valor());

        assertEquals(List.of(1000, 1200, 1500), service.serieUf(m, Indicador.POPULACAO, "RO"));
        assertEquals(List.of(1000, 1200, 1500), service.serieUf(m, Indicador.POPULACAO, "11"));
    }

    @Test
    void serieDoMunicipioTemNullNaLacuna() {
        assertEquals(java.util.Arrays.asList(800, null, 900),
                service.serieMunicipio(m, Indicador.POPULACAO, "1200401"));
        assertNull(service.serieMunicipio(m, Indicador.POPULACAO, "9999999"));
        assertNull(service.serieUf(m, Indicador.POPULACAO, "SP"));
    }

    @Test
    void anoInexistenteEh400ComMensagemDoC() {
        ApiException e = assertThrows(ApiException.class,
                () -> service.consultar(m, Indicador.POPULACAO, false, null, 2020, null, null, 0));
        assertEquals(400, e.status());
        assertEquals("ano 2020 inexistente na série", e.getMessage());
    }

    @Test
    void anoAusenteNaoEhTratadoComoZero() {
        assertThrows(ApiException.class,
                () -> service.consultar(m, Indicador.POPULACAO, false, null, 0, null, null, 0));
    }
}
