package br.gov.dashboard.cli;

import br.gov.dashboard.geografia.GeoNivel;
import br.gov.dashboard.ingestao.comum.Baixador;
import br.gov.dashboard.ingestao.comum.CacheArquivos;
import br.gov.dashboard.ingestao.comum.Fontes;
import br.gov.dashboard.ingestao.comum.IngestaoException;
import br.gov.dashboard.ingestao.geo.PreparadorMalhas;
import br.gov.dashboard.ingestao.ibge.IngestaoIbge;
import br.gov.dashboard.ingestao.saude.ProcessadorCnes;
import br.gov.dashboard.modelo.LeitorIndice;
import br.gov.dashboard.modelo.Modelo;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Set;

/**
 * Ponto de entrada da ingestão, com a precedência e os códigos de saída do
 * main.c: 0 sucesso, 1 falha de ingestão, 2 fonte desconhecida ou não
 * implementada.
 *
 * {@code --atualizar} sem fonte e {@code --atualizar ibge} fazem o dado do
 * IBGE e, em seguida, as malhas — é o comportamento do C, mesmo que o texto
 * de ajuda diga "só o IBGE". O front busca as malhas no load e um banco com
 * dado e sem mapa derruba o mapa em silêncio.
 *
 * {@code --atualizar cnes} é a mudança intencional da migração: o C recusava
 * a fonte mesmo tendo o processador pronto em dataset_saude.c. Leitos e Dengue
 * continuam recusados porque o C de referência nunca os implementou — só
 * declarou as colunas no header — e não há oráculo nem consumidor que defina o
 * formato de saída. Inventá-lo aqui seria criar contrato, não portá-lo.
 */
public final class IngestaoCli {

    private static final Set<String> CONHECIDAS =
            Set.of("ibge", "malhas", "cnes", "dengue", "leitos");

    private IngestaoCli() {
    }

    public static int executar(String fonte) {
        String alvo = (fonte == null || fonte.isEmpty()) ? "ibge" : fonte;
        if (!CONHECIDAS.contains(alvo)) {
            System.err.printf("--atualizar %s não é uma fonte conhecida. "
                    + "Use ibge, malhas ou cnes.%n", alvo);
            return 2;
        }

        Path conf = Paths.get("fontes.conf");
        Path processed = Paths.get("data", "processed");
        Fontes fontes = new Fontes(
                new CacheArquivos(Paths.get("data", "cache")),
                new Baixador.Http(),
                conf);

        try {
            return switch (alvo) {
                case "ibge" -> {
                    new IngestaoIbge(fontes, processed).executar();
                    preparaMalhas(fontes, processed);
                    resumo(processed);
                    yield 0;
                }
                case "malhas" -> {
                    preparaMalhas(fontes, processed);
                    yield 0;
                }
                case "cnes" -> {
                    executaCnes(fontes, processed);
                    yield 0;
                }
                default -> {
                    // Leitos e Dengue não existem no C: dataset_saude.c só
                    // implementa CNES. A mensagem diz isso, em vez de prometer
                    // uma fase futura do Java que não tem contrato para seguir.
                    System.err.printf("--atualizar %s ainda não é implementado: o C de "
                            + "referência nunca processou Leitos nem Dengue "
                            + "(dataset_saude.c só tem CNES), então não há formato "
                            + "de saída nem oráculo para portar.%n"
                            + "O que existe hoje: --atualizar ibge, malhas e cnes.%n", alvo);
                    yield 2;
                }
            };
        } catch (IngestaoException e) {
            System.err.println(e.getMessage());
            return 1;
        }
    }

    private static void executaCnes(Fontes fontes, Path processed) {
        Path zip;
        try {
            zip = fontes.caminhoEmCache("cnes");
        } catch (IOException e) {
            throw new IngestaoException("não consegui baixar o CNES "
                    + "(fonte cnes em fontes.conf)", e);
        }
        Path destino = processed.resolve("cnes.json");
        ProcessadorCnes.executar(zip, destino, 0);
        try {
            System.out.printf("CNES processado em %s (%d bytes)%n",
                    destino, Files.size(destino));
        } catch (IOException e) {
            // O arquivo acabou de ser gravado; se o stat falhar, o dado está
            // lá e o tamanho é só informativo.
        }
    }

    private static void preparaMalhas(Fontes fontes, Path processed) {
        PreparadorMalhas preparador = new PreparadorMalhas(fontes, processed);
        preparador.preparar(GeoNivel.MUNICIPIOS);
        preparador.preparar(GeoNivel.ESTADOS);
    }

    /** Medição final do C: malhas processadas e um resumo do modelo. */
    private static void resumo(Path processed) {
        GeoInfo.executar(processed);
        try {
            Modelo m = LeitorIndice.ler(processed.resolve("modelo.idx"));
            System.out.printf("%-12s %7d municípios, %d anos, %d UFs%n",
                    "modelo:", m.nLocal(), m.nAnos(), m.nUfs());
        } catch (IOException e) {
            // O C também só imprime o resumo quando o índice abre; a ingestão
            // já terminou com sucesso, então não é motivo para exit != 0.
        }
    }
}
