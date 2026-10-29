package br.gov.dashboard.ingestao.comum;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ArquivoZipTest {

    @TempDir
    Path temp;

    private Path zip(String nome, String... entradas) throws Exception {
        Path p = temp.resolve(nome);
        try (OutputStream os = Files.newOutputStream(p);
             ZipOutputStream z = new ZipOutputStream(os, StandardCharsets.ISO_8859_1)) {
            for (int i = 0; i < entradas.length; i += 2) {
                z.putNextEntry(new ZipEntry(entradas[i]));
                z.write(entradas[i + 1].getBytes(StandardCharsets.ISO_8859_1));
                z.closeEntry();
            }
        }
        return p;
    }

    @Test
    void listaEExtraiPorNome() throws Exception {
        Path p = zip("a.zip", "LEIAME.txt", "ignorar", "dados.csv", "a;b\n1;2\n");
        assertEquals(2, ArquivoZip.lista(p).size());
        assertEquals("dados.csv", ArquivoZip.primeiraCsv(p));
        assertArrayEquals("a;b\n1;2\n".getBytes(StandardCharsets.ISO_8859_1),
                ArquivoZip.extrai(p, "dados.csv"));
    }

    @Test
    void primeiraCsvIgnoraEntradaCurtaESemSufixo() throws Exception {
        Path p = zip("b.zip", "csv", "x", "dados.CSV", "y");
        assertNull(ArquivoZip.primeiraCsv(p));
    }

    @Test
    void entradaAusenteEhErro() throws Exception {
        Path p = zip("c.zip", "dados.csv", "x");
        assertThrows(Exception.class, () -> ArquivoZip.extrai(p, "outra.csv"));
        assertThrows(Exception.class, () -> ArquivoZip.lista(temp.resolve("nao-existe.zip")));
    }

    @Test
    void aceitaZipComDeflate() throws Exception {
        Path p = temp.resolve("d.zip");
        byte[] grande = "linha;".repeat(1000).getBytes(StandardCharsets.ISO_8859_1);
        try (OutputStream os = Files.newOutputStream(p);
             ZipOutputStream z = new ZipOutputStream(os)) {
            z.putNextEntry(new ZipEntry("dados.csv"));
            z.write(grande);
            z.closeEntry();
        }
        assertTrue(Files.size(p) < grande.length);
        assertArrayEquals(grande, ArquivoZip.extrai(p, "dados.csv"));
    }
}
