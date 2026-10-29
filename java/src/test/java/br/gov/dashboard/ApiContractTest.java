package br.gov.dashboard;

import br.gov.dashboard.comum.ApiFallbackController;
import br.gov.dashboard.comum.ErroHandler;
import br.gov.dashboard.config.DashboardProperties;
import br.gov.dashboard.consulta.ConsultaController;
import br.gov.dashboard.consulta.ConsultaService;
import br.gov.dashboard.geografia.GeoController;
import br.gov.dashboard.geografia.GeoJsonService;
import br.gov.dashboard.modelo.Modelo;
import br.gov.dashboard.modelo.Modelos;
import br.gov.dashboard.opcoes.OpcoesController;
import br.gov.dashboard.valores.ValoresController;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.MvcResult;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

class ApiContractTest {

    @TempDir
    Path temp;

    private MockMvc mvc;
    private final ObjectMapper mapper = new ObjectMapper();

    @BeforeEach
    void prepara() throws Exception {
        Modelo m = Modelos.fixture();
        ConsultaService service = new ConsultaService();

        Path processed = temp.resolve("data").resolve("processed");
        Files.createDirectories(processed);
        Files.writeString(processed.resolve("geo_estados.json"),
                "{\"type\":\"FeatureCollection\",\"features\":["
                        + "{\"type\":\"Feature\",\"properties\":{\"codarea\":\"11\"},\"geometry\":null},"
                        + "{\"type\":\"Feature\",\"properties\":{\"codarea\":\"12\"},\"geometry\":null}]}",
                StandardCharsets.UTF_8);
        Files.writeString(processed.resolve("geo_municipios.json"),
                "{\"type\":\"FeatureCollection\",\"features\":["
                        + "{\"type\":\"Feature\",\"properties\":{\"codarea\":\"1100015\"},\"geometry\":null},"
                        + "{\"type\":\"Feature\",\"properties\":{\"codarea\":\"1200401\"},\"geometry\":null}]}",
                StandardCharsets.UTF_8);

        DashboardProperties props = new DashboardProperties(temp.resolve("data").toString(), "web");
        GeoJsonService geo = new GeoJsonService(props, mapper);

        mvc = MockMvcBuilders.standaloneSetup(
                        new ConsultaController(m, service),
                        new OpcoesController(m, service),
                        new ValoresController(m, service),
                        new GeoController(geo),
                        new ApiFallbackController())
                .setControllerAdvice(new ErroHandler())
                .build();
    }

    @Test
    void consultaTemEnvelopeDaSpec() throws Exception {
        mvc.perform(get("/api/consulta").param("ano", "2021"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.consulta.indicador").value("populacao"))
                .andExpect(jsonPath("$.consulta.nivel").value("municipio"))
                .andExpect(jsonPath("$.total").value(3))
                .andExpect(jsonPath("$.kpis.media").value(1650))
                .andExpect(jsonPath("$.registros[0].cod").value("4106902"))
                .andExpect(jsonPath("$.registros[0].valor").value(2100));
    }

    @Test
    void ausenteViraNullENaoSomeDoJson() throws Exception {
        MvcResult r = mvc.perform(get("/api/consulta").param("ano", "2021"))
                .andExpect(status().isOk())
                .andReturn();
        JsonNode raiz = mapper.readTree(r.getResponse().getContentAsString());
        JsonNode rio = raiz.at("/registros/2");
        assertEquals("1200401", rio.path("cod").asText());
        assertTrue(rio.has("valor"), "a chave valor tem de existir");
        assertTrue(rio.get("valor").isNull(), "ausente é null, nunca 0");
    }

    @Test
    void serieSoSaiComRecorte() throws Exception {
        mvc.perform(get("/api/consulta").param("ano", "2021"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.serie").doesNotExist())
                .andExpect(jsonPath("$.serie_anos").doesNotExist());

        mvc.perform(get("/api/consulta").param("ano", "2021").param("cod", "1200401"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.serie[0]").value(800))
                .andExpect(jsonPath("$.serie[1]").isEmpty())
                .andExpect(jsonPath("$.serie[2]").value(900))
                .andExpect(jsonPath("$.serie_anos[1]").value(2021));
    }

    @Test
    void errosUsamOEnvelopeEMensagemDoC() throws Exception {
        mvc.perform(get("/api/consulta"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.erro.codigo").value(400))
                .andExpect(jsonPath("$.erro.mensagem").value("ano \"\" inválido"));

        mvc.perform(get("/api/consulta").param("ano", "20a4"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.erro.mensagem").value("ano \"20a4\" inválido"));

        mvc.perform(get("/api/consulta").param("ano", "2020"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.erro.mensagem").value("ano 2020 inexistente na série"));

        mvc.perform(get("/api/consulta").param("ano", "2021").param("nivel", "uf").param("cod", "1100015"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.erro.mensagem")
                        .value("nível UF não aceita o filtro cod=: a linha já é a UF"));

        mvc.perform(get("/api/consulta").param("ano", "2021").param("indicador", "xyz"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.erro.mensagem").value("indicador inválido"));
    }

    @Test
    void rotaDesconhecidaResponde404NoEnvelope() throws Exception {
        mvc.perform(get("/api/nao-existe"))
                .andExpect(status().isNotFound())
                .andExpect(jsonPath("$.erro.codigo").value(404))
                .andExpect(jsonPath("$.erro.mensagem").value("rota não encontrada"));
    }

    @Test
    void opcoesComUfDevolveSoAEla() throws Exception {
        mvc.perform(get("/api/opcoes").param("uf", "RO"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.anos[0]").value(2019))
                .andExpect(jsonPath("$.ufs.length()").value(1))
                .andExpect(jsonPath("$.ufs[0]").value("RO"))
                .andExpect(jsonPath("$.uf").value("RO"));
    }

    @Test
    void valoresTrazemNullENomesSobDemanda() throws Exception {
        mvc.perform(get("/api/valores").param("ano", "2021").param("nomes", "1"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.total").value(3))
                .andExpect(jsonPath("$.com_dado").value(2))
                .andExpect(jsonPath("$.valores['1200401']").isEmpty())
                .andExpect(jsonPath("$.nomes['1200401']").value("Rio Branco"));

        mvc.perform(get("/api/valores").param("ano", "2021"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.nomes").doesNotExist());
    }

    @Test
    void geografiaFiltraPorPrefixoDoCodarea() throws Exception {
        mvc.perform(get("/api/geo/municipios").param("uf", "RO"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.features.length()").value(1))
                .andExpect(jsonPath("$.features[0].properties.codarea").value("1100015"));

        mvc.perform(get("/api/geo/estados").param("uf", "XX"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.erro.mensagem").value("UF inválida"));
    }
}
