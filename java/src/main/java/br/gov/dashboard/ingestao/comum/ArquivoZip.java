package br.gov.dashboard.ingestao.comum;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Leitura de ZIP do JDK, com a mesma escolha de entrada do {@code zip.c}. */
public final class ArquivoZip {

    private ArquivoZip() {
    }

    /** Nomes das entradas na ordem do diretório central. */
    public static List<String> lista(Path caminho) throws IOException {
        List<String> nomes = new ArrayList<>();
        try (ZipFile z = new ZipFile(caminho.toFile(), StandardCharsets.ISO_8859_1)) {
            var en = z.entries();
            while (en.hasMoreElements()) {
                nomes.add(en.nextElement().getName());
            }
        }
        return Collections.unmodifiableList(nomes);
    }

    /** Primeira entrada terminada em ".csv", ou null. */
    public static String primeiraCsv(Path caminho) throws IOException {
        for (String nome : lista(caminho)) {
            if (nome.length() > 4 && nome.endsWith(".csv")) return nome;
        }
        return null;
    }

    public static byte[] extrai(Path caminho, String nome) throws IOException {
        try (ZipFile z = new ZipFile(caminho.toFile(), StandardCharsets.ISO_8859_1)) {
            ZipEntry e = z.getEntry(nome);
            if (e == null || e.isDirectory()) throw new IOException("entrada ausente: " + nome);
            try (InputStream in = z.getInputStream(e)) {
                return in.readAllBytes();
            }
        }
    }
}
