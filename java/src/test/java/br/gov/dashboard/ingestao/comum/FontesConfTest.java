package br.gov.dashboard.ingestao.comum;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class FontesConfTest {

    @TempDir
    Path temp;

    private Path conf(String texto) throws IOException {
        Path p = temp.resolve("fontes.conf");
        Files.writeString(p, texto, StandardCharsets.UTF_8);
        return p;
    }

    @Test
    void leSecoesComComentariosEEspacos() throws Exception {
        List<FontesConf.Fonte> fontes = FontesConf.ler(conf("""
                # comentário
                [cnes]
                url    = https://exemplo/cnes.zip
                dataset = cnes.csv
                ttl     = 604800
                tem_utf8 = 1

                [ibge]
                  url_zip = https://exemplo/ibge.json
                """));
        assertEquals(2, fontes.size());

        FontesConf.Fonte cnes = FontesConf.obtem(fontes, "cnes");
        assertEquals("https://exemplo/cnes.zip", cnes.url());
        assertEquals("cnes.csv", cnes.dataset());
        assertEquals(604800, cnes.ttl());
        assertTrue(cnes.temUtf8());

        FontesConf.Fonte ibge = FontesConf.obtem(fontes, "ibge");
        assertEquals("https://exemplo/ibge.json", ibge.url());
        assertEquals(FontesConf.TTL_NENHUM, ibge.ttl());
        assertEquals("", ibge.nome());
    }

    @Test
    void ttlInvalidoFicaAusenteEmVezDeVirarZero() throws Exception {
        List<FontesConf.Fonte> fontes = FontesConf.ler(conf("""
                [s]
                url = https://exemplo/s
                ttl = 12x
                """));
        assertEquals(FontesConf.TTL_NENHUM, fontes.get(0).ttl());
    }

    @Test
    void chavesDesconhecidasSaoIgnoradas() throws Exception {
        List<FontesConf.Fonte> fontes = FontesConf.ler(conf("""
                [s]
                url = https://exemplo/s
                coluna_que_nao_existe = 1
                """));
        assertEquals(1, fontes.size());
        assertEquals("https://exemplo/s", fontes.get(0).url());
    }

    @Test
    void arquivoAusenteOuSemSecaoEhErro() throws Exception {
        assertThrows(IOException.class, () -> FontesConf.ler(temp.resolve("nao-existe.conf")));
        assertThrows(IOException.class, () -> FontesConf.ler(conf("# só comentário\n")));
    }

    @Test
    void obtemDevolveNullQuandoNaoAcha() throws Exception {
        List<FontesConf.Fonte> fontes = FontesConf.ler(conf("[s]\nurl = https://x\n"));
        assertNull(FontesConf.obtem(fontes, "outra"));
    }

    @Test
    void inteiroEstritoAceitaSinalENadaMais() {
        assertEquals(-1, FontesConf.inteiroEstrito("-1"));
        assertEquals(42, FontesConf.inteiroEstrito("  +42"));
        assertNull(FontesConf.inteiroEstrito("42x"));
        assertNull(FontesConf.inteiroEstrito(""));
        assertNull(FontesConf.inteiroEstrito("99999999999"));
    }
}
