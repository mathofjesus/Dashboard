package br.gov.dashboard.ingestao.geo;

import br.gov.dashboard.comum.Texto;
import br.gov.dashboard.geografia.GeoNivel;
import br.gov.dashboard.ingestao.comum.Fontes;
import br.gov.dashboard.ingestao.comum.IngestaoException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * Preparação das malhas do IBGE: une, simplifica por Douglas-Peucker (0,05°)
 * e reescreve em graus×1000, o porte de {@code geo_prepara}.
 *
 * A saída é montada com o mesmo formato textual do C. Como a simplificação e
 * o arredondamento são os mesmos, o arquivo tende a sair byte a byte igual ao
 * do C para a mesma entrada — e é assim que a fase F8 é verificada.
 */
public final class PreparadorMalhas {

    private static final double TOLERANCIA = 0.05;
    private static final String[] CHAVES_MUNICIPIOS = {"ibge_malhas_municipios"};
    private static final String[] CHAVES_ESTADOS = {
            "ibge_malhas_estados_1", "ibge_malhas_estados_2", "ibge_malhas_estados_3",
            "ibge_malhas_estados_4", "ibge_malhas_estados_5"
    };

    private final Fontes fontes;
    private final Path processedDir;
    private final ObjectMapper mapper = new ObjectMapper();

    public PreparadorMalhas(Fontes fontes, Path processedDir) {
        this.fontes = fontes;
        this.processedDir = processedDir;
    }

    public void preparar(GeoNivel nivel) {
        String[] chaves = nivel == GeoNivel.MUNICIPIOS ? CHAVES_MUNICIPIOS : CHAVES_ESTADOS;
        List<Feature> acumulado = new ArrayList<>();

        try {
            // Primeiro o cache, sem abrir o .conf: é o caso comum depois do
            // primeiro --atualizar e o que permite rodar sem rede.
            for (String chave : chaves) {
                byte[] bruto = fontes.doCache(chave);
                if (bruto == null) bruto = fontes.garante(chave);
                leColecao(bruto, acumulado);
            }
        } catch (IOException e) {
            throw new IngestaoException("não consegui processar as malhas. Rode com rede "
                    + "(curl baixa de data/cache/) ou confira fontes.conf.", e);
        }

        if (acumulado.isEmpty()) {
            throw new IngestaoException("não consegui processar as malhas. Rode com rede "
                    + "(curl baixa de data/cache/) ou confira fontes.conf.");
        }
        acumulado.sort(Comparator
                .comparing((Feature f) -> f.codarea, Texto::compararBytes)
                .thenComparingInt(f -> f.ordem));

        String saida = montaSaida(acumulado);
        Path destino = processedDir.resolve(nivel == GeoNivel.MUNICIPIOS
                ? "geo_municipios.json" : "geo_estados.json");
        grava(destino, saida.getBytes(StandardCharsets.UTF_8));
    }

    /* ------------------------------------------------------------------ */
    /* leitura                                                             */
    /* ------------------------------------------------------------------ */

    private void leColecao(byte[] json, List<Feature> acumulado) {
        JsonNode raiz;
        try {
            raiz = mapper.readTree(json);
        } catch (IOException e) {
            throw new IngestaoException("malha do IBGE não é JSON válido", e);
        }
        JsonNode feats = raiz == null ? null : raiz.path("features");
        if (feats == null || !feats.isArray()) {
            throw new IngestaoException("malha do IBGE sem \"features\"");
        }
        for (JsonNode feat : feats) {
            Feature f = leFeature(feat, acumulado.size());
            if (f != null) acumulado.add(f);
        }
    }

    private Feature leFeature(JsonNode feat, int ordem) {
        JsonNode props = feat.path("properties");
        JsonNode cod = props.path("codarea");
        if (!cod.isTextual() || cod.asText().isEmpty()) return null;

        Feature f = new Feature();
        f.codarea = cod.asText();
        f.ordem = ordem;
        JsonNode cent = props.path("centroide");
        f.cx = arredonda(num(cent, 0));
        f.cy = arredonda(num(cent, 1));

        JsonNode geom = feat.path("geometry");
        if (!geom.isObject()) return null;
        String tipo = geom.path("type").isTextual() ? geom.path("type").asText() : "";
        JsonNode coords = geom.path("coordinates");
        if (!coords.isArray()) return null;

        if ("Polygon".equals(tipo)) {
            f.tipo = 'P';
            for (JsonNode anel : coords) {
                processaAnel(anel, f, 0);
            }
            return f.aneis.isEmpty() ? null : f;
        }
        if ("MultiPolygon".equals(tipo)) {
            f.tipo = 'M';
            for (int i = 0; i < coords.size(); i++) {
                JsonNode poly = coords.get(i);
                if (!poly.isArray()) continue;
                for (JsonNode anel : poly) {
                    processaAnel(anel, f, i);
                }
            }
            return f.aneis.isEmpty() ? null : f;
        }
        // GeometryCollection e tipos desconhecidos são descartados, nunca
        // transformados em geometria inventada.
        return null;
    }

    private void processaAnel(JsonNode anelJson, Feature f, int poligono) {
        if (!anelJson.isArray()) return;
        List<long[]> cru = new ArrayList<>(anelJson.size());
        for (JsonNode pt : anelJson) {
            if (!pt.isArray() || pt.size() < 2) return; // anel inteiro é descartado
            cru.add(new long[]{arredonda(num(pt, 0)), arredonda(num(pt, 1))});
        }
        if (cru.size() < 4) return;

        long[] xs = new long[cru.size()];
        long[] ys = new long[cru.size()];
        for (int i = 0; i < cru.size(); i++) {
            xs[i] = cru.get(i)[0];
            ys[i] = cru.get(i)[1];
        }
        List<long[]> simp = simplifica(xs, ys, TOLERANCIA);
        if (simp.size() < 4) return;
        f.aneis.add(new Anel(simp, poligono));
        if (f.nPoligonos < poligono + 1) f.nPoligonos = poligono + 1;
    }

    private static double num(JsonNode arr, int i) {
        if (arr == null || !arr.isArray() || i >= arr.size()) return 0.0;
        JsonNode v = arr.get(i);
        return v.isNumber() ? v.asDouble() : 0.0;
    }

    /* ------------------------------------------------------------------ */
    /* simplificação                                                       */
    /* ------------------------------------------------------------------ */

    /** Arredonda graus para graus×1000, com a mesma expressão do C. */
    static long arredonda(double v) {
        double x = v * 1000.0;
        if (x < 0) x -= 0.5;
        else x += 0.5;
        return (long) x;
    }

    private static double dist2AoSegmento(long px, long py, long ax, long ay,
                                          long bx, long by) {
        long dx = bx - ax, dy = by - ay;
        long px1 = px - ax, py1 = py - ay;
        long len2 = dx * dx + dy * dy;

        if (len2 == 0) return (double) (px1 * px1 + py1 * py1);
        long t = px1 * dx + py1 * dy;
        if (t <= 0) return (double) (px1 * px1 + py1 * py1);
        if (t >= len2) {
            long qx = px - bx, qy = py - by;
            return (double) (qx * qx + qy * qy);
        }
        long cross = px1 * dy - py1 * dx;
        return (double) cross * (double) cross / (double) len2;
    }

    /**
     * Douglas-Peucker iterativo, tratando o anel como polilinha aberta de 0 a
     * n-2 e fechando o resultado no fim. A explicação longa está em geo.c:
     * rodar de 0 a n-1 com o anel fechado faz o primeiro segmento ter
     * comprimento zero e a simplificação perder o sentido.
     */
    static List<long[]> simplifica(long[] x, long[] y, double tolGraus) {
        int n = x.length;
        List<long[]> saida = new ArrayList<>();
        if (n < 4) return saida;

        double tol2 = tolGraus * tolGraus * 1000.0 * 1000.0;
        int ultimo = n - 2;
        boolean[] marcas = new boolean[n];
        marcas[0] = true;
        marcas[ultimo] = true;
        int nMarcados = 2;

        int[] pilha = new int[128];
        int npilha = 0;
        pilha[0] = 0;
        pilha[1] = ultimo;
        npilha = 1;

        while (npilha > 0) {
            npilha--;
            int ini = pilha[npilha * 2];
            int fim = pilha[npilha * 2 + 1];
            if (fim <= ini + 1) continue;

            int pior = 0;
            double piorD = -1.0;
            for (int i = ini + 1; i < fim; i++) {
                double d = dist2AoSegmento(x[i], y[i], x[ini], y[ini], x[fim], y[fim]);
                if (d > piorD) {
                    piorD = d;
                    pior = i;
                }
            }
            if (!(piorD > tol2)) continue;
            if (pior <= ini || pior >= fim) continue;

            marcas[pior] = true;
            nMarcados++;
            if (npilha + 2 > pilha.length / 2) {
                int[] maior = new int[pilha.length * 2];
                System.arraycopy(pilha, 0, maior, 0, pilha.length);
                pilha = maior;
            }
            pilha[npilha * 2] = ini;
            pilha[npilha * 2 + 1] = pior;
            npilha++;
            pilha[npilha * 2] = pior;
            pilha[npilha * 2 + 1] = fim;
            npilha++;
        }

        // Um anel válido tem no mínimo 3 vértices distintos; se o DP comeu
        // até 2, promove os pontos que mais se afastam das pontas.
        while (nMarcados < 3 && ultimo >= 2) {
            int melhor = 0;
            double melhorD = -1.0;
            for (int i = 1; i <= ultimo; i++) {
                if (marcas[i]) continue;
                double d = dist2AoSegmento(x[i], y[i], x[0], y[0], x[ultimo], y[ultimo]);
                if (d > melhorD) {
                    melhorD = d;
                    melhor = i;
                }
            }
            if (melhor == 0) break;
            marcas[melhor] = true;
            nMarcados++;
        }

        for (int i = 0; i <= ultimo; i++) {
            if (marcas[i]) saida.add(new long[]{x[i], y[i]});
        }
        if (saida.size() >= 3) saida.add(new long[]{x[0], y[0]});
        return saida;
    }

    /* ------------------------------------------------------------------ */
    /* escrita                                                             */
    /* ------------------------------------------------------------------ */

    private static String montaSaida(List<Feature> fs) {
        StringBuilder sb = new StringBuilder(1_600_000);
        sb.append("{\"type\":\"FeatureCollection\",\"features\":[");
        for (int i = 0; i < fs.size(); i++) {
            if (i > 0) sb.append(',');
            escreveFeature(sb, fs.get(i));
        }
        sb.append("]}");
        return sb.toString();
    }

    private static void escreveFeature(StringBuilder sb, Feature f) {
        sb.append("{\"type\":\"Feature\",\"properties\":{\"codarea\":\"")
                .append(f.codarea)
                .append("\",\"centroide\":[").append(f.cx).append(',').append(f.cy)
                .append("]},\"geometry\":{\"type\":\"")
                .append(f.tipo == 'M' ? "MultiPolygon" : "Polygon")
                .append("\",\"coordinates\":[");

        if (f.tipo == 'M') {
            for (int p = 0; p < f.nPoligonos; p++) {
                if (p > 0) sb.append(',');
                sb.append('[');
                boolean primeiro = true;
                for (Anel a : f.aneis) {
                    if (a.poligono != p) continue;
                    if (!primeiro) sb.append(',');
                    primeiro = false;
                    escreveAnel(sb, a.pontos);
                }
                sb.append(']');
            }
        } else {
            for (int i = 0; i < f.aneis.size(); i++) {
                if (i > 0) sb.append(',');
                escreveAnel(sb, f.aneis.get(i).pontos);
            }
        }
        sb.append("]}}");
    }

    private static void escreveAnel(StringBuilder sb, List<long[]> pontos) {
        sb.append('[');
        for (int i = 0; i < pontos.size(); i++) {
            if (i > 0) sb.append(',');
            long[] p = pontos.get(i);
            sb.append('[').append(p[0]).append(',').append(p[1]).append(']');
        }
        sb.append(']');
    }

    private void grava(Path destino, byte[] bytes) {
        try {
            Files.createDirectories(processedDir);
            Path tmp = destino.resolveSibling(destino.getFileName() + ".tmp");
            Files.write(tmp, bytes);
            try {
                Files.move(tmp, destino, StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (java.nio.file.AtomicMoveNotSupportedException e) {
                Files.move(tmp, destino, StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (IOException e) {
            throw new IngestaoException("não consegui gravar " + destino, e);
        }
    }

    private static final class Feature {
        String codarea;
        long cx, cy;
        char tipo;
        int nPoligonos;
        int ordem;
        final List<Anel> aneis = new ArrayList<>();
    }

    private record Anel(List<long[]> pontos, int poligono) {
    }
}
