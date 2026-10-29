package br.gov.dashboard.ingestao.comum;

import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

class CsvTest {

    private static byte[] latin1(String s) {
        return s.getBytes(StandardCharsets.ISO_8859_1);
    }

    @Test
    void detectaDelimitadorForaDeAspas() {
        assertEquals(';', Csv.detectaDelimitador(latin1("a;b;c")));
        assertEquals(',', Csv.detectaDelimitador(latin1("a,b,c")));
        assertEquals(';', Csv.detectaDelimitador(latin1("\"a;b\";c")));
        assertEquals(';', Csv.detectaDelimitador(latin1("a;b,c")));
        assertEquals(',', Csv.detectaDelimitador(new byte[0]));
    }

    @Test
    void aceitaBomECrase() {
        byte[] bom = {(byte) 0xEF, (byte) 0xBB, (byte) 0xBF};
        byte[] corpo = "cod;nome\n1;São Paulo\n".getBytes(StandardCharsets.UTF_8);
        byte[] bytes = new byte[bom.length + corpo.length];
        System.arraycopy(bom, 0, bytes, 0, bom.length);
        System.arraycopy(corpo, 0, bytes, bom.length, corpo.length);

        Csv csv = Csv.abrir(bytes, ';', StandardCharsets.UTF_8);
        assertEquals(2, csv.numLinhas());
        assertEquals("cod", csv.cabecalho(0));
        assertEquals("São Paulo", csv.campo(1, 1));
    }

    @Test
    void converteLatin1NaLeitura() {
        Csv csv = Csv.abrir(latin1("nome\nPOLICLÍNICA\n"), ';', StandardCharsets.ISO_8859_1);
        assertEquals("POLICLÍNICA", csv.campo(1, 0));
    }

    @Test
    void aspasComSeparadorEAspasDuplas() {
        Csv csv = Csv.abrir(latin1("\"a;b\";\"diz \"\"oi\"\"\"\n"), ';', StandardCharsets.ISO_8859_1);
        assertEquals(1, csv.numLinhas());
        assertEquals("a;b", csv.campo(0, 0));
        assertEquals("diz \"oi\"", csv.campo(0, 1));
    }

    @Test
    void crlfNaoCriaLinhaVaziaFinal() {
        Csv csv = Csv.abrir(latin1("a;b\r\n1;2\r\n"), ';', StandardCharsets.ISO_8859_1);
        assertEquals(2, csv.numLinhas());
        assertEquals("2", csv.campo(1, 1));
    }

    @Test
    void campoForaDosLimitesEhVazio() {
        Csv csv = Csv.abrir(latin1("a;b\n1\n"), ';', StandardCharsets.ISO_8859_1);
        assertEquals("", csv.campo(1, 1));
        assertEquals("", csv.campo(9, 0));
        assertEquals(-1, csv.indiceColuna("nao-existe"));
        assertEquals(0, csv.indiceColuna("a"));
    }

    @Test
    void abrirNuloDevolveNulo() {
        assertNull(Csv.abrir(null, ';', StandardCharsets.UTF_8));
    }
}
