package br.gov.dashboard.ingestao.ibge;

import br.gov.dashboard.comum.Texto;
import br.gov.dashboard.ingestao.comum.Fontes;
import br.gov.dashboard.ingestao.comum.IngestaoException;
import br.gov.dashboard.modelo.EscritorIndice;
import br.gov.dashboard.modelo.Indicador;
import br.gov.dashboard.modelo.Localidade;
import br.gov.dashboard.modelo.Modelo;
import br.gov.dashboard.modelo.Uf;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Ingestão do IBGE: Localidades + série de população do SIDRA (tabela 6579,
 * variável 9324), o porte de {@code model_ingere_ibge}.
 *
 * A reprodução é deliberadamente fiel, inclusive nos detalhes estranhos:
 * a região da localidade sai vazia (o C copia o objeto {@code regiao} como
 * texto e recebe ""), o nome por extenso da UF também fica vazio (o C compara
 * o código numérico D1C com a sigla), e a lista de anos vem do nível de UF e
 * é deduplicada em ordem crescente. É o que faz o {@code modelo.idx} sair
 * byte a byte igual ao do C.
 */
public final class IngestaoIbge {

    /** ANOS_POR_LOTE do model.c. */
    private static final int ANOS_POR_LOTE = 3;

    private final Fontes fontes;
    private final Path processedDir;
    private final ObjectMapper mapper = new ObjectMapper();

    public IngestaoIbge(Fontes fontes, Path processedDir) {
        this.fontes = fontes;
        this.processedDir = processedDir;
    }

    public Modelo executar() {
        System.out.println("IBGE Localidades: baixando a lista de municípios...");
        System.out.flush();

        List<Localidade> localidades;
        List<Uf> ufs;
        try {
            byte[] bruto = fontes.garante("ibge_localidades");
            DadosLocais dados = ingereLocalidades(bruto);
            localidades = dados.localidades();
            ufs = dados.ufs();
        } catch (IOException e) {
            throw new IngestaoException("não consegui baixar a lista de municípios do IBGE "
                    + "(fonte ibge_localidades em fontes.conf)", e);
        }
        System.out.printf("  %d municípios, %d UFs%n", localidades.size(), ufs.size());

        System.out.println("SIDRA tabela 6579 (variável 9324): descobrindo os anos...");
        System.out.flush();

        int[] anos;
        try {
            byte[] bruto = fontes.garante("ibge_sidra_ufs");
            anos = ingereAnosENomesUf(bruto, ufs);
        } catch (IOException e) {
            throw new IngestaoException("não consegui baixar a série do SIDRA no nível de UF "
                    + "(fonte ibge_sidra_ufs em fontes.conf)", e);
        }

        StringBuilder lista = new StringBuilder();
        for (int i = 0; i < anos.length; i++) {
            if (i > 0) lista.append(", ");
            lista.append(anos[i]);
        }
        System.out.println("  anos: " + lista);

        int nLocal = localidades.size();
        long passoLong = (long) nLocal * anos.length;
        long totalLong = passoLong * Indicador.TOTAL;
        if (passoLong > Integer.MAX_VALUE || totalLong > Integer.MAX_VALUE - 8) {
            throw new IngestaoException("sem memória para a série");
        }
        int passo = (int) passoLong;
        int[] valores = new int[(int) totalLong];
        Arrays.fill(valores, Modelo.AUSENTE);

        Map<String, Integer> indicePorCodigo = new HashMap<>(nLocal * 2);
        for (int i = 0; i < nLocal; i++) {
            indicePorCodigo.putIfAbsent(localidades.get(i).cod(), i);
        }
        Map<Integer, Integer> indicePorAno = new HashMap<>(anos.length * 2);
        for (int i = 0; i < anos.length; i++) indicePorAno.put(anos[i], i);

        int nLote = (anos.length + ANOS_POR_LOTE - 1) / ANOS_POR_LOTE;
        for (int i = 0; i < anos.length; i += ANOS_POR_LOTE) {
            int ate = (anos.length - i < ANOS_POR_LOTE) ? anos.length - 1 : i + ANOS_POR_LOTE - 1;
            StringBuilder sufixo = new StringBuilder("/");
            for (int j = i; j <= ate; j++) {
                if (j > i) sufixo.append(',');
                sufixo.append(anos[j]);
            }
            String chave = "ibge_sidra_mun_" + anos[i] + "_" + anos[ate];

            System.out.printf("  lote %d/%d, anos %s...%n",
                    i / ANOS_POR_LOTE + 1, nLote, sufixo.substring(1));
            System.out.flush();

            byte[] bruto;
            try {
                bruto = fontes.baixa("ibge_sidra_municipios", sufixo.toString(), chave);
            } catch (IOException e) {
                throw new IngestaoException("não consegui baixar a série do SIDRA para os anos "
                        + sufixo.substring(1) + " (fonte ibge_sidra_municipios em fontes.conf)", e);
            }

            int preenchidos = ingereLoteMunicipios(bruto, indicePorCodigo, indicePorAno,
                    valores, passo, anos.length, nLocal);
            System.out.printf("    %d valores preenchidos%n", preenchidos);
        }

        System.err.println("aviso: por enquanto só a população foi ingerida; "
                + "estabelecimentos, leitos, UTI e dengue continuam sem "
                + "valor (dados do Ministério da Saúde são tarefa "
                + "posterior)");

        Modelo modelo = new Modelo(
                localidades.toArray(Localidade[]::new),
                ufs.toArray(Uf[]::new),
                anos, valores);
        Path destino = processedDir.resolve("modelo.idx");
        try {
            EscritorIndice.gravar(modelo, destino);
        } catch (IOException e) {
            throw new IngestaoException("não consegui gravar " + destino, e);
        }
        System.out.printf("índice gravado em %s (%d municípios × %d anos)%n",
                destino, nLocal, anos.length);
        return modelo;
    }

    /* ------------------------------------------------------------------ */
    /* localidades                                                         */
    /* ------------------------------------------------------------------ */

    private record DadosLocais(List<Localidade> localidades, List<Uf> ufs) {
    }

    private DadosLocais ingereLocalidades(byte[] json) {
        JsonNode raiz;
        try {
            raiz = mapper.readTree(json);
        } catch (IOException e) {
            throw new IngestaoException("IBGE Localidades: resposta não é JSON válido", e);
        }
        if (raiz == null || !raiz.isArray()) {
            throw new IngestaoException("IBGE Localidades: resposta não é JSON válido");
        }
        if (raiz.isEmpty()) {
            throw new IngestaoException("IBGE Localidades: a lista de municípios veio vazia");
        }

        List<Localidade> locais = new ArrayList<>(raiz.size());
        for (JsonNode mun : raiz) {
            JsonNode uf = mun.path("microrregiao").path("mesorregiao").path("UF");
            String sigla = texto(uf.path("sigla"));
            String nome = texto(mun.path("nome"));
            JsonNode id = mun.path("id");
            if (!uf.isObject() || sigla.isEmpty() || nome.isEmpty() || !id.isNumber()) continue;

            int codigo = (int) id.asDouble();
            String cod = Integer.toString(codigo);
            if (cod.length() != 7) continue;

            // O C copia o objeto "regiao" como se fosse texto e recebe "";
            // reproduzir isso é o que mantém o índice byte a byte igual.
            String regiao = uf.path("regiao").isTextual() ? uf.path("regiao").asText() : "";
            locais.add(new Localidade(cod, nome, sigla, regiao));
        }
        if (locais.isEmpty()) {
            throw new IngestaoException("IBGE Localidades: nenhum município tem microrregião — "
                    + "o formato da resposta mudou?");
        }
        locais.sort((a, b) -> Texto.compararBytes(a.cod(), b.cod()));

        Map<String, Uf> porSigla = new LinkedHashMap<>();
        for (Localidade l : locais) {
            porSigla.putIfAbsent(l.uf(), new Uf(l.uf(), "", l.regiao()));
        }
        List<Uf> ufs = new ArrayList<>(porSigla.values());
        ufs.sort((a, b) -> Texto.compararBytes(a.sigla(), b.sigla()));
        return new DadosLocais(locais, ufs);
    }

    /* ------------------------------------------------------------------ */
    /* SIDRA                                                               */
    /* ------------------------------------------------------------------ */

    private int[] ingereAnosENomesUf(byte[] json, List<Uf> ufs) {
        JsonNode raiz;
        try {
            raiz = mapper.readTree(json);
        } catch (IOException e) {
            throw new IngestaoException("SIDRA 6579 (nível de UF): resposta não é JSON válido", e);
        }
        if (raiz == null || !raiz.isArray() || raiz.size() < 2) {
            throw new IngestaoException("SIDRA 6579 (nível de UF): a série veio vazia");
        }

        List<Integer> anos = new ArrayList<>();
        for (int i = 1; i < raiz.size(); i++) {
            JsonNode linha = raiz.get(i);
            String d1c = texto(linha.path("D1C"));
            int ano = inteiroDoSidra(texto(linha.path("D3C")));
            if (d1c.isEmpty() || ano <= 0) continue;
            anos.add(ano);

            // Paridade literal com o C: D1C é o código numérico ("11"), então
            // a comparação com a sigla ("RO") nunca casa e o nome fica vazio.
            if (d1c.length() != 2) continue;
            String cod = d1c.substring(0, 2);
            for (Uf u : ufs) {
                if (!u.sigla().equals(cod)) continue;
                // Uf é imutável; o caso em que o C preencheria o nome não
                // acontece com a API real. Mantido para documentar a regra.
                break;
            }
        }
        if (anos.isEmpty()) {
            throw new IngestaoException("SIDRA 6579 (nível de UF): nenhum ano reconhecido");
        }
        int[] ordenados = anos.stream().mapToInt(Integer::intValue).sorted().distinct().toArray();
        if (ordenados.length == 0) {
            throw new IngestaoException("SIDRA 6579 (nível de UF): nenhum ano reconhecido");
        }
        return ordenados;
    }

    private int ingereLoteMunicipios(byte[] json, Map<String, Integer> indicePorCodigo,
                                     Map<Integer, Integer> indicePorAno, int[] valores,
                                     int passo, int nAnos, int nLocal) {
        JsonNode raiz;
        try {
            raiz = mapper.readTree(json);
        } catch (IOException e) {
            throw new IngestaoException("SIDRA 6579 (municípios): resposta não é JSON válido", e);
        }
        if (raiz == null || !raiz.isArray() || raiz.size() < 2) {
            throw new IngestaoException("SIDRA 6579 (municípios): o lote veio vazio");
        }

        int preenchidos = 0;
        for (int i = 1; i < raiz.size(); i++) {
            JsonNode linha = raiz.get(i);
            String cod = texto(linha.path("D1C"));
            if (cod.isEmpty()) continue;
            Integer idxLocal = indicePorCodigo.get(cod);
            if (idxLocal == null) continue;

            int ano = inteiroDoSidra(texto(linha.path("D3C")));
            Integer idxAno = indicePorAno.get(ano);
            if (idxAno == null) continue;

            int valor = inteiroDoSidra(texto(linha.path("V")));
            int base = Indicador.POPULACAO.codigo() * passo + idxLocal * nAnos + idxAno;
            valores[base] = valor;
            if (valor != Modelo.AUSENTE) preenchidos++;
        }
        return preenchidos;
    }

    /**
     * O texto do SIDRA é um número limpo ou uma lacuna: "26919", "1.234" ou
     * ".." / "..." / "-". Ponto, espaço e tab são tolerados; qualquer outro
     * caractere é lacuna. Nunca devolve 0 por falta de informação.
     */
    static int inteiroDoSidra(String s) {
        if (s == null || s.isEmpty()) return Modelo.AUSENTE;
        long v = 0;
        int digitos = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c < '0' || c > '9') {
                if (c != '.' && c != ' ' && c != '\t') return Modelo.AUSENTE;
                continue;
            }
            if (v > 2147483647L / 10) return Modelo.AUSENTE;
            v = v * 10 + (c - '0');
            digitos++;
        }
        if (digitos == 0 || v > 2147483647L) return Modelo.AUSENTE;
        return (int) v;
    }

    private static String texto(JsonNode n) {
        return n != null && n.isTextual() ? n.asText() : "";
    }
}
