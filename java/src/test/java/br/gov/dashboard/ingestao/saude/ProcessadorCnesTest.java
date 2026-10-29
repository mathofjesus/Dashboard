package br.gov.dashboard.ingestao.saude;

import br.gov.dashboard.ingestao.comum.IngestaoException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ProcessadorCnesTest {

    private static final ObjectMapper MAPPER = new ObjectMapper();

    private static final String CABECALHO = String.join(";",
            "CO_CNES", "CO_UNIDADE", "CO_UF", "CO_IBGE", "NU_CNPJ_MANTENEDORA",
            "NO_RAZAO_SOCIAL", "NO_FANTASIA", "CO_NATUREZA_ORGANIZACAO",
            "DS_NATUREZA_ORGANIZACAO", "TP_GESTAO", "CO_NIVEL_HIERARQUIA",
            "DS_NIVEL_HIERARQUIA", "CO_ESFERA_ADMINISTRATIVA",
            "DS_ESFERA_ADMINISTRATIVA", "CO_ATIVIDADE", "TP_UNIDADE", "CO_CEP",
            "NO_LOGRADOURO", "NU_ENDERECO", "NO_BAIRRO", "NU_TELEFONE",
            "NU_LATITUDE", "NU_LONGITUDE", "CO_TURNO_ATENDIMENTO",
            "DS_TURNO_ATENDIMENTO", "NU_CNPJ", "NO_EMAIL", "CO_NATUREZA_JUR",
            "ST_CENTRO_CIRURGICO", "ST_CENTRO_OBSTETRICO", "ST_CENTRO_NEONATAL",
            "ST_ATEND_HOSPITALAR", "ST_SERVICO_APOIO",
            "ST_ATEND_AMBULATORIAL", "CO_MOTIVO_DESAB", "CO_AMBULATORIAL_SUS");

    @TempDir
    Path temp;

    private static String aspas(String s) {
        return "\"" + s.replace("\"", "\"\"") + "\"";
    }

    /** Monta uma linha com as 36 colunas, nas posições reais do arquivo. */
    private static String linha(String codigo, String razao, String fantasia,
                                String nivel, String lat, String lon) {
        List<String> c = new ArrayList<>();
        c.add("2000001");
        c.add("2000001");
        c.add("11");
        c.add(codigo);
        c.add("00000000000151");
        c.add(aspas(razao));
        c.add(aspas(fantasia));
        c.add("1234");
        c.add(aspas("SAUDE"));
        c.add(aspas("PUBLICO"));
        c.add("S");
        c.add(aspas(nivel));
        c.add("1");
        c.add(aspas("MUNICIPAL"));
        c.add("2");
        c.add("01");
        c.add(aspas("76800-000"));
        c.add(aspas("RUA A"));
        c.add("1");
        c.add(aspas("CENTRO"));
        c.add(aspas("11 99999-0000"));
        c.add(aspas(lat));
        c.add(aspas(lon));
        c.add("12");
        c.add(aspas("24H"));
        c.add(aspas("00000000000191"));
        c.add(aspas("saude@exemplo.br"));
        c.add(aspas("PUBLICIDADE"));
        for (int i = 0; i < 8; i++) c.add(aspas("NAO"));
        return String.join(";", c);
    }

    private static Path zip(Path destino, String nomeEntrada, String csv) throws Exception {
        try (OutputStream os = Files.newOutputStream(destino);
             ZipOutputStream z = new ZipOutputStream(os, StandardCharsets.ISO_8859_1)) {
            z.putNextEntry(new ZipEntry("LEIAME.txt"));
            z.write("ignorar".getBytes(StandardCharsets.ISO_8859_1));
            z.closeEntry();
            z.putNextEntry(new ZipEntry(nomeEntrada));
            z.write(csv.getBytes(StandardCharsets.ISO_8859_1));
            z.closeEntry();
        }
        return destino;
    }

    private static String fixture() {
        return String.join("\n",
                CABECALHO,
                linha("1100015", "POLICLÍNICA SÃO JOÃO", "UBS \"CENTRAL\"",
                        "02 - CENTRO DE SAUDE", "-8.123456", "-63.654321"),
                linha("..", "SEM MUNICIPIO", "", "", "", ".."),
                linha("", "TRACO \\ E ASPAS \"A\"", "x", "x", "abc", "  -1.5"))
                + "\n";
    }

    @Test
    void podaEmJsonLinesValidoComAsChavesDoCabecalho() throws Exception {
        Path z = zip(temp.resolve("cnes.zip"), "cnes_estabelecimentos.csv", fixture());
        Path saida = temp.resolve("cnes.json");
        ProcessadorCnes.executar(z, saida, 0);

        List<String> linhas = Files.readAllLines(saida, StandardCharsets.UTF_8);
        assertEquals(4, linhas.size(), "cabeçalho + três registros");

        JsonNode cab = MAPPER.readTree(linhas.get(0));
        assertEquals("cnes", cab.path("formato").asText());
        assertEquals(3, cab.path("linhas").asInt());
        assertEquals(List.of("CO_IBGE", "NO_RAZAO_SOCIAL", "NO_FANTASIA",
                        "DS_NIVEL_HIERARQUIA", "NU_LATITUDE", "NU_LONGITUDE"),
                textos(cab.path("colunas")));
        assertEquals(List.of("codarea", "razao", "fantasia", "nivel", "lat", "lon"),
                textos(cab.path("chaves")));

        JsonNode r1 = MAPPER.readTree(linhas.get(1));
        assertEquals(List.of("codarea", "razao", "fantasia", "nivel", "lat", "lon"),
                textosDeChaves(r1.fieldNames()));
        assertEquals("1100015", r1.path("codarea").asText());
        assertEquals("POLICLÍNICA SÃO JOÃO", r1.path("razao").asText());
        assertEquals("UBS \"CENTRAL\"", r1.path("fantasia").asText());
        assertEquals("02 - CENTRO DE SAUDE", r1.path("nivel").asText());
        assertTrue(linhas.get(1).contains("\"lat\":-8.123456,"));
        assertTrue(linhas.get(1).contains("\"lon\":-63.654321"));

        JsonNode r2 = MAPPER.readTree(linhas.get(2));
        assertEquals(-1, r2.path("codarea").asInt());
        assertEquals("..", r2.path("codigo_bruto").asText());
        assertTrue(r2.path("lat").isNull());
        assertTrue(r2.path("lon").isNull());

        JsonNode r3 = MAPPER.readTree(linhas.get(3));
        assertEquals(-1, r3.path("codarea").asInt());
        assertEquals("", r3.path("codigo_bruto").asText());
        assertEquals("TRACO \\ E ASPAS \"A\"", r3.path("razao").asText());
        assertTrue(linhas.get(3).contains("\"lat\":0.000000,"));
        assertTrue(linhas.get(3).contains("\"lon\":-1.500000"));
    }

    @Test
    void limiteDeAmostraNaoMenteNoCabecalho() throws Exception {
        Path z = zip(temp.resolve("cnes.zip"), "cnes_estabelecimentos.csv", fixture());
        Path saida = temp.resolve("cnes-limite.json");
        ProcessadorCnes.executar(z, saida, 2);

        List<String> linhas = Files.readAllLines(saida, StandardCharsets.UTF_8);
        assertEquals(3, linhas.size(), "cabeçalho + dois registros");
        JsonNode cab = MAPPER.readTree(linhas.get(0));
        assertEquals(3, cab.path("linhas").asInt(),
                "o total anunciado é o do arquivo, não o desta amostra");
    }

    @Test
    void colunaAusenteFalhaAntesDeEscrever() throws Exception {
        String semLat = String.join("\n",
                CABECALHO.replace("NU_LATITUDE;", ""),
                linha("1100015", "X", "Y", "Z", "-8.0", "-63.0")) + "\n";
        // Remove o campo correspondente de cada linha para manter 35 colunas.
        semLat = semLat.replace("\"-8.0\";", "");

        Path z = zip(temp.resolve("sem-lat.zip"), "cnes.csv", semLat);
        Path saida = temp.resolve("nao-deve-existir.json");
        IngestaoException e = assertThrows(IngestaoException.class,
                () -> ProcessadorCnes.executar(z, saida, 0));
        assertTrue(e.getMessage().contains("NU_LATITUDE"));
        assertFalse(Files.exists(saida));
    }

    @Test
    void zipSemCsvEhErro() throws Exception {
        Path z = temp.resolve("sem-csv.zip");
        try (OutputStream os = Files.newOutputStream(z);
             ZipOutputStream zip = new ZipOutputStream(os)) {
            zip.putNextEntry(new ZipEntry("LEIAME.txt"));
            zip.write("nada".getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
        }
        IngestaoException e = assertThrows(IngestaoException.class,
                () -> ProcessadorCnes.executar(z, temp.resolve("x.json"), 0));
        assertTrue(e.getMessage().contains("nenhum .csv"));
    }

    private static List<String> textos(Iterable<JsonNode> nos) {
        List<String> out = new ArrayList<>();
        for (JsonNode n : nos) out.add(n.asText());
        return out;
    }

    private static List<String> textosDeChaves(Iterator<String> it) {
        List<String> out = new ArrayList<>();
        while (it.hasNext()) out.add(it.next());
        return out;
    }
}
