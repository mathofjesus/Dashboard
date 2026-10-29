package br.gov.dashboard.ingestao.comum;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;

/** Contrato mínimo de download; permite testar a ingestão sem rede. */
public interface Baixador {

    /**
     * Baixa a URL e devolve o corpo cru.
     *
     * @throws IOException quando a URL é inválida, o servidor responde erro,
     *                     ou o tempo esgota.
     */
    byte[] baixar(String url) throws IOException;

    /** Implementação real: HTTP/HTTPS com o mesmo teto de tempo do curl. */
    final class Http implements Baixador {

        /** O mesmo limite do http_client.c: 512 MB. */
        public static final long LIMITE_BYTES = 512L * 1024L * 1024L;
        /** O mesmo --max-time do http_client.c. */
        public static final Duration TEMPO_MAXIMO = Duration.ofSeconds(120);

        private final HttpClient cliente;

        public Http() {
            this.cliente = HttpClient.newBuilder()
                    .connectTimeout(Duration.ofSeconds(20))
                    .followRedirects(HttpClient.Redirect.NORMAL)
                    .build();
        }

        @Override
        public byte[] baixar(String url) throws IOException {
            if (!urlAceita(url)) {
                throw new IOException("URL recusada: " + url);
            }
            HttpRequest req;
            try {
                req = HttpRequest.newBuilder(URI.create(url))
                        .timeout(TEMPO_MAXIMO)
                        .header("User-Agent", "dashboard/0.1 (migracao Java)")
                        .GET()
                        .build();
            } catch (IllegalArgumentException e) {
                throw new IOException("URL inválida: " + url, e);
            }

            HttpResponse<byte[]> resposta;
            try {
                resposta = cliente.send(req, HttpResponse.BodyHandlers.ofByteArray());
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IOException("download interrompido: " + url, e);
            }
            if (resposta.statusCode() < 200 || resposta.statusCode() >= 300) {
                throw new IOException("HTTP " + resposta.statusCode() + " em " + url);
            }
            byte[] corpo = resposta.body();
            if (corpo == null || corpo.length == 0) {
                throw new IOException("resposta vazia em " + url);
            }
            if (corpo.length > LIMITE_BYTES) {
                throw new IOException("corpo maior que 512 MB em " + url);
            }
            return corpo;
        }

        /** Só http e https, sem caracteres de controle na linha de comando. */
        static boolean urlAceita(String url) {
            if (url == null) return false;
            if (!url.startsWith("http://") && !url.startsWith("https://")) return false;
            for (int i = 0; i < url.length(); i++) {
                char c = url.charAt(i);
                if (c < 0x20 || c == 0x7f) return false;
            }
            return true;
        }
    }
}
