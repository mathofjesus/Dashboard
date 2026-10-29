package br.gov.dashboard.cli;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;

/** {@code --geo-info}: mede as malhas processadas sem parsear o GeoJSON. */
public final class GeoInfo {

    private GeoInfo() {
    }

    public static int executar(Path processed) {
        System.out.printf(Locale.ROOT, "tolerância da simplificação: %.2f graus%n", 0.05);
        System.out.println("coordenadas: graus×1000 (inteiras)");
        mede("estados:", processed.resolve("geo_estados.json"));
        mede("municípios:", processed.resolve("geo_municipios.json"));
        return 0;
    }

    private static void mede(String rotulo, Path caminho) {
        try {
            if (!Files.isRegularFile(caminho)) {
                System.out.printf(Locale.ROOT, "%-12s ainda não processado (%s)%n", rotulo, caminho);
                return;
            }
            byte[] bytes = Files.readAllBytes(caminho);
            System.out.printf(Locale.ROOT, "%-12s %7d features, %9d bytes processados%n",
                    rotulo, contaFeatures(bytes), bytes.length);
        } catch (IOException e) {
            System.out.printf(Locale.ROOT, "%-12s ainda não processado (%s)%n", rotulo, caminho);
        }
    }

    /** Conta {@code "type":"Feature"} em bytes, como conta_features do main.c. */
    private static int contaFeatures(byte[] bytes) {
        byte[] agulha = "\"type\":\"Feature\"".getBytes(StandardCharsets.UTF_8);
        int n = 0;
        for (int i = 0; i + agulha.length <= bytes.length; i++) {
            int j = 0;
            while (j < agulha.length && bytes[i + j] == agulha[j]) j++;
            if (j == agulha.length) {
                n++;
                i += agulha.length - 1;
            }
        }
        return n;
    }
}
