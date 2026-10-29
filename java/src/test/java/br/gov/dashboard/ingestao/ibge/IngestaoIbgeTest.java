package br.gov.dashboard.ingestao.ibge;

import br.gov.dashboard.ingestao.comum.Baixador;
import br.gov.dashboard.ingestao.comum.CacheArquivos;
import br.gov.dashboard.ingestao.comum.Fontes;
import br.gov.dashboard.modelo.Indicador;
import br.gov.dashboard.modelo.LeitorIndice;
import br.gov.dashboard.modelo.Modelo;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class IngestaoIbgeTest {

    @TempDir
    Path temp;

    private static final String LOCALIDADES = """
            [
              {"id": 1100015, "nome": "Alta Floresta D'Oeste",
               "microrregiao": {"mesorregiao": {"UF": {"sigla": "RO",
                 "nome": "Rondônia", "regiao": {"id": 1, "sigla": "N"}}}}},
              {"id": 1100023, "nome": "Ariquemes",
               "microrregiao": {"mesorregiao": {"UF": {"sigla": "RO",
                 "nome": "Rondônia", "regiao": {"id": 1, "sigla": "N"}}}}},
              {"id": 3550308, "nome": "São Paulo",
               "microrregiao": {"mesorregiao": {"UF": {"sigla": "SP",
                 "nome": "São Paulo", "regiao": {"id": 3, "sigla": "SE"}}}}},
              {"id": 9999999, "nome": "Sem Microrregião"}
            ]
            """;

    private static final String SIDRA_UFS = """
            [
              {},
              {"D1C": "11", "D3C": "2001", "V": "1"},
              {"D1C": "35", "D3C": "2002", "V": "2"},
              {"D1C": "11", "D3C": "2002", "V": "3"}
            ]
            """;

    private static final String SIDRA_MUNICIPIOS = """
            [
              {},
              {"D1C": "1100015", "D3C": "2001", "V": "12.345"},
              {"D1C": "1100015", "D3C": "2002", "V": ".."},
              {"D1C": "1100023", "D3C": "2001", "V": "7"},
              {"D1C": "3550308", "D3C": "2002", "V": "1.234.567"},
              {"D1C": "3550308", "D3C": "2001", "V": "-3"},
              {"D1C": "9999999", "D3C": "2001", "V": "5"}
            ]
            """;

    private void grava(CacheArquivos cache, String chave, String json) {
        assertTrue(cache.grava(chave, json.getBytes(StandardCharsets.UTF_8)));
    }

    @Test
    void ingereLocalidadesESidraSemRede() throws Exception {
        CacheArquivos cache = new CacheArquivos(temp.resolve("cache"));
        grava(cache, "ibge_localidades", LOCALIDADES);
        grava(cache, "ibge_sidra_ufs", SIDRA_UFS);
        grava(cache, "ibge_sidra_mun_2001_2002", SIDRA_MUNICIPIOS);

        Baixador semRede = url -> {
            throw new AssertionError("não deveria baixar: " + url);
        };
        Path processed = temp.resolve("processed");
        Fontes fontes = new Fontes(cache, semRede, temp.resolve("fontes.conf"));

        Modelo m = new IngestaoIbge(fontes, processed).executar();

        assertEquals(3, m.nLocal());
        assertEquals(2, m.nAnos());
        assertEquals(2, m.nUfs());
        assertEquals(2001, m.ano(0));
        assertEquals(2002, m.ano(1));

        assertEquals("1100015", m.local(0).cod());
        assertEquals("Alta Floresta D'Oeste", m.local(0).nome());
        assertEquals("RO", m.local(0).uf());
        // O C copia o objeto "regiao" como texto e recebe vazio; a paridade
        // com o modelo.idx real depende de manter esse detalhe.
        assertEquals("", m.local(0).regiao());
        assertEquals("3550308", m.local(2).cod());

        assertEquals("RO", m.uf(0).sigla());
        assertEquals("SP", m.uf(1).sigla());
        assertEquals("", m.uf(0).nome());
        assertEquals("", m.uf(0).regiao());

        assertEquals(12345, m.valor(Indicador.POPULACAO, 0, 2001));
        assertEquals(Modelo.AUSENTE, m.valor(Indicador.POPULACAO, 0, 2002));
        assertEquals(7, m.valor(Indicador.POPULACAO, 1, 2001));
        assertEquals(1234567, m.valor(Indicador.POPULACAO, 2, 2002));
        assertEquals(Modelo.AUSENTE, m.valor(Indicador.POPULACAO, 2, 2001),
                "valor negativo não é população");

        for (Indicador ind : Indicador.values()) {
            if (ind == Indicador.POPULACAO) continue;
            for (int i = 0; i < m.nLocal(); i++) {
                for (int a = 0; a < m.nAnos(); a++) {
                    assertEquals(Modelo.AUSENTE, m.valor(ind, i, a));
                }
            }
        }

        Path idx = processed.resolve("modelo.idx");
        assertTrue(Files.isRegularFile(idx));
        Modelo relido = LeitorIndice.ler(idx);
        assertEquals(m.nLocal(), relido.nLocal());
        assertEquals(m.valor(Indicador.POPULACAO, 2, 2002),
                relido.valor(Indicador.POPULACAO, 2, 2002));
    }

    @Test
    void anosDoLoteSaoOsAnosDescobertos() throws Exception {
        assertEquals(12345, IngestaoIbge.inteiroDoSidra("12.345"));
        assertEquals(-1, IngestaoIbge.inteiroDoSidra(".."));
        assertEquals(-1, IngestaoIbge.inteiroDoSidra("..."));
        assertEquals(-1, IngestaoIbge.inteiroDoSidra("-3"));
        assertEquals(-1, IngestaoIbge.inteiroDoSidra(""));
        assertEquals(1234567, IngestaoIbge.inteiroDoSidra("1.234.567"));
    }
}
