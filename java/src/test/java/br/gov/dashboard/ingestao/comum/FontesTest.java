package br.gov.dashboard.ingestao.comum;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class FontesTest {

    @TempDir
    Path temp;

    private Path conf(String texto) throws IOException {
        Path p = temp.resolve("fontes.conf");
        Files.writeString(p, texto, StandardCharsets.UTF_8);
        return p;
    }

    /** Baixador de mentira: registra URLs e entrega respostas na ordem. */
    private static final class Fake implements Baixador {
        final List<String> urls = new ArrayList<>();
        final Deque<Object> respostas = new ArrayDeque<>();

        Fake resposta(byte[] corpo) {
            respostas.add(corpo);
            return this;
        }

        Fake falha() {
            respostas.add(new IOException("rede fora"));
            return this;
        }

        @Override
        public byte[] baixar(String url) throws IOException {
            urls.add(url);
            Object r = respostas.removeFirst();
            if (r instanceof IOException e) throw e;
            return (byte[]) r;
        }
    }

    @Test
    void baixaGravaNoCacheEDepoisNaoTocaNaRede() throws Exception {
        Path c = conf("""
                [s]
                url = https://exemplo/s
                ttl = 60
                """);
        Fake fake = new Fake().resposta("abc".getBytes(StandardCharsets.UTF_8));
        CacheArquivos cache = new CacheArquivos(temp.resolve("cache"));
        Fontes fontes = new Fontes(cache, fake, c);

        assertArrayEquals("abc".getBytes(StandardCharsets.UTF_8), fontes.garante("s"));
        assertEquals(1, fake.urls.size());
        assertEquals(60, cache.ttl("s"));
        assertTrue(cache.tem("s"));

        assertArrayEquals("abc".getBytes(StandardCharsets.UTF_8), fontes.garante("s"));
        assertEquals(1, fake.urls.size(), "a segunda chamada veio do cache");
        assertEquals(cache.caminho("s"), fontes.caminhoEmCache("s"));
    }

    @Test
    void tentaDuasVezesAntesDeDesistir() throws Exception {
        Path c = conf("[s]\nurl = https://exemplo/s\n");
        Fake fake = new Fake().falha().resposta("ok".getBytes(StandardCharsets.UTF_8));
        Fontes fontes = new Fontes(new CacheArquivos(temp.resolve("cache")), fake, c);

        assertArrayEquals("ok".getBytes(StandardCharsets.UTF_8), fontes.garante("s"));
        assertEquals(2, fake.urls.size());
    }

    @Test
    void sufixoVaiParaAUrlEChaveDeCacheEhPropria() throws Exception {
        Path c = conf("[s]\nurl = https://exemplo/base\n");
        Fake fake = new Fake()
                .resposta("a".getBytes(StandardCharsets.UTF_8))
                .resposta("b".getBytes(StandardCharsets.UTF_8));
        CacheArquivos cache = new CacheArquivos(temp.resolve("cache"));
        Fontes fontes = new Fontes(cache, fake, c);

        fontes.baixa("s", "/2001,2002", "s_2001_2002");
        fontes.baixa("s", "/2003", "s_2003");
        assertEquals(List.of("https://exemplo/base/2001,2002", "https://exemplo/base/2003"),
                fake.urls);
        assertTrue(cache.tem("s_2001_2002"));
        assertTrue(cache.tem("s_2003"));
    }

    @Test
    void fonteDesconhecidaEhErro() throws Exception {
        Fontes fontes = new Fontes(new CacheArquivos(temp.resolve("cache")),
                new Fake(), conf("[s]\nurl = https://exemplo/s\n"));
        IOException e = assertThrows(IOException.class, () -> fontes.garante("nao-existe"));
        assertTrue(e.getMessage().contains("nao-existe"));
    }

    @Test
    void falhaDuasVezesPropaga() throws Exception {
        Fontes fontes = new Fontes(new CacheArquivos(temp.resolve("cache")),
                new Fake().falha().falha(), conf("[s]\nurl = https://exemplo/s\n"));
        assertThrows(IOException.class, () -> fontes.garante("s"));
    }
}
