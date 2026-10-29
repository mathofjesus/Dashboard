package br.gov.dashboard.modelo;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class IndiceTest {

    @TempDir
    Path temp;

    @Test
    void amostraVaiEVoltaComOsMesmosBytes() throws Exception {
        Path a = temp.resolve("a.idx");
        Path b = temp.resolve("b.idx");
        Amostra.gravar(a);
        Modelo lido = LeitorIndice.ler(a);
        EscritorIndice.gravar(lido, b);

        assertArrayEquals(Files.readAllBytes(a), Files.readAllBytes(b));
        assertEquals(3, lido.nLocal());
        assertEquals(3, lido.nAnos());
        assertEquals(3, lido.nUfs());
        assertEquals("Alta Floresta D'Oeste", lido.local(0).nome());
        assertEquals("AC", lido.uf(0).sigla());
        assertEquals(30995, lido.valor(Indicador.POPULACAO, 0, 2021));
        assertEquals(Modelo.AUSENTE, lido.valor(Indicador.DENGUE, 0, 2021));
    }

    @Test
    void magicErradaEhRecusada() throws Exception {
        Path p = temp.resolve("ruim.idx");
        Files.write(p, "NAO-E-INDICE\n".getBytes());
        assertThrows(Exception.class, () -> LeitorIndice.ler(p));
    }

    @Test
    void indiceTruncadoEhRecusado() throws Exception {
        Path p = temp.resolve("cheio.idx");
        Amostra.gravar(p);
        byte[] cheio = Files.readAllBytes(p);
        Path truncado = temp.resolve("truncado.idx");
        Files.write(truncado, java.util.Arrays.copyOf(cheio, cheio.length - 7));
        assertThrows(Exception.class, () -> LeitorIndice.ler(truncado));
    }

    @Test
    void ufAceitaSiglaCodigoENome() {
        Modelo m = Modelos.fixture();
        assertEquals(2, m.ufDeSigla("RO"));
        assertEquals(2, m.ufDeSigla("ro"));
        assertEquals(2, m.ufDeSigla("11"));
        assertEquals(m.nUfs(), m.ufDeSigla("R"));
        assertEquals(m.nUfs(), m.ufDeSigla("SP"));
        // O C compara os dois primeiros caracteres sem exigir tamanho 2:
        // "Rondônia" casa com RO. É o comportamento que a API expõe hoje.
        assertEquals(2, m.ufDeSigla("Rondônia"));
    }
}
