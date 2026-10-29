package br.gov.dashboard.ingestao.comum;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.time.Instant;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CacheArquivosTest {

    @TempDir
    Path temp;

    @Test
    void gravaELeBytesCrUs() {
        CacheArquivos cache = new CacheArquivos(temp);
        byte[] dados = {0, 1, 2, (byte) 0xff};
        assertTrue(cache.grava("chave", dados));
        assertArrayEquals(dados, cache.le("chave"));
        assertTrue(cache.tem("chave"));
        assertFalse(Files.exists(temp.resolve("chave.tmp")));
    }

    @Test
    void ttlAusenteSignificaImutavel() throws Exception {
        CacheArquivos cache = new CacheArquivos(temp);
        cache.grava("s", "x".getBytes(StandardCharsets.UTF_8));
        assertEquals(CacheArquivos.TTL_NENHUM, cache.ttl("s"));
        assertTrue(cache.tem("s"));
    }

    @Test
    void ttlExpiradoEhRecusado() throws Exception {
        CacheArquivos cache = new CacheArquivos(temp);
        cache.grava("s", "x".getBytes(StandardCharsets.UTF_8));
        cache.setTtl("s", 10);
        Files.setLastModifiedTime(cache.caminho("s"),
                FileTime.from(Instant.now().minusSeconds(3600)));
        assertFalse(cache.tem("s"));
    }

    @Test
    void ttlCorrompidoFalhaFechado() throws Exception {
        CacheArquivos cache = new CacheArquivos(temp);
        cache.grava("s", "x".getBytes(StandardCharsets.UTF_8));
        Files.writeString(cache.caminho("s").resolveSibling("s.ttl"), "10 lixo");
        assertEquals(CacheArquivos.TTL_CORROMPIDO, cache.ttl("s"));
        assertFalse(cache.tem("s"));
    }

    @Test
    void ttlNegativoRemoveOSidecar() throws Exception {
        CacheArquivos cache = new CacheArquivos(temp);
        cache.grava("s", "x".getBytes(StandardCharsets.UTF_8));
        cache.setTtl("s", 10);
        assertTrue(Files.exists(temp.resolve("s.ttl")));
        cache.setTtl("s", -1);
        assertFalse(Files.exists(temp.resolve("s.ttl")));
        assertTrue(cache.tem("s"));
    }

    @Test
    void nomeLongoUsaResumoDoValorInteiro() {
        String chave = "a".repeat(100);
        String nome = CacheArquivos.nomeDoArquivo(chave);
        assertEquals("a".repeat(79) + "_1900dafab04497b7", nome);
        assertEquals(96, nome.length());

        // Duas chaves que só diferem no fim não podem cair no mesmo arquivo.
        String outra = "a".repeat(99) + "b";
        assertFalse(nome.equals(CacheArquivos.nomeDoArquivo(outra)));
    }

    @Test
    void nomeSeguroNaoEscapaDoDiretorio() {
        CacheArquivos cache = new CacheArquivos(temp);
        Path p = cache.caminho("../../etc/passwd");
        assertEquals(temp, p.getParent());
        assertEquals("______etc_passwd", p.getFileName().toString());
        assertNull(cache.caminho(""));
    }
}
