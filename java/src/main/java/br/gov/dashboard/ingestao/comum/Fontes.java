package br.gov.dashboard.ingestao.comum;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;

/**
 * O caminho completo de {@code fontes_garente} / {@code fontes_baixa}.
 *
 * Primeiro o cache — sem abrir o .conf e sem tocar na rede; é o caminho de
 * quem já rodou {@code --atualizar}. Só quando a chave não está no cache (ou
 * expirou) é que o .conf é lido, a URL é montada e o download acontece. Uma
 * falha de download tem uma segunda tentativa: o transiente de rede é comum,
 * duas falhas seguidas já são outra coisa.
 */
public final class Fontes {

    private final CacheArquivos cache;
    private final Baixador baixador;
    private final Path conf;

    public Fontes(CacheArquivos cache, Baixador baixador, Path conf) {
        this.cache = cache;
        this.baixador = baixador;
        this.conf = conf;
    }

    public CacheArquivos cache() {
        return cache;
    }

    /** Só o cache: não abre o .conf nem baixa. Devolve null se não houver. */
    public byte[] doCache(String chave) {
        if (!cache.tem(chave)) return null;
        return cache.le(chave);
    }

    /** Garante a fonte padrão (sem sufixo), usando o id como chave de cache. */
    public byte[] garante(String id) throws IOException {
        return baixa(id, null, id);
    }

    /**
     * Garante a fonte com sufixo na URL e chave de cache própria — o que a
     * série municipal do SIDRA precisa para não sobrescrever um lote com o
     * seguinte.
     */
    public byte[] baixa(String id, String sufixo, String chaveCache) throws IOException {
        String chave = (chaveCache == null || chaveCache.isEmpty()) ? id : chaveCache;

        byte[] cacheado = doCache(chave);
        if (cacheado != null && cacheado.length > 0) return cacheado;

        List<FontesConf.Fonte> fontes;
        try {
            fontes = FontesConf.ler(conf);
        } catch (IOException e) {
            throw new IOException("não consegui ler " + conf, e);
        }
        FontesConf.Fonte fonte = FontesConf.obtem(fontes, id);
        if (fonte == null || fonte.url().isEmpty()) {
            throw new IOException("fonte " + id + " não existe em " + conf);
        }

        String url = fonte.url() + (sufixo == null ? "" : sufixo);
        byte[] corpo;
        try {
            corpo = baixador.baixar(url);
        } catch (IOException primeira) {
            System.err.println("  o download falhou; tentando mais uma vez...");
            corpo = baixador.baixar(url);
        }
        if (corpo == null || corpo.length == 0) {
            throw new IOException("download vazio em " + url);
        }

        if (!cache.grava(chave, corpo)) {
            // Perder o cache é mais barato que perder a ingestão inteira.
            System.err.printf("aviso: não gravei a fonte %s no cache%n", chave);
        } else if (fonte.ttl() != FontesConf.TTL_NENHUM) {
            cache.setTtl(chave, fonte.ttl());
        }
        return corpo;
    }

    /** Garante a fonte e devolve o caminho do arquivo em cache (para ZIPs). */
    public Path caminhoEmCache(String id) throws IOException {
        garante(id);
        Path p = cache.caminho(id);
        if (p == null) throw new IOException("chave de cache inválida: " + id);
        return p;
    }
}
