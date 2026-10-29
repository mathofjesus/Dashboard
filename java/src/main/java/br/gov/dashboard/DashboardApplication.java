package br.gov.dashboard;

import br.gov.dashboard.cli.CliOptions;
import br.gov.dashboard.cli.GeoInfo;
import br.gov.dashboard.cli.IngestaoCli;
import br.gov.dashboard.modelo.Amostra;
import br.gov.dashboard.modelo.LeitorIndice;
import br.gov.dashboard.modelo.Modelo;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.ConfigurationPropertiesScan;

import java.io.IOException;
import java.io.PrintStream;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Entrada do dashboard: lê as opções, faz a ingestão pedida ou sobe o
 * servidor HTTP em 127.0.0.1.
 *
 * O portão de primeira execução é o mesmo do C: o índice é lido de verdade,
 * não só testado com stat(). Um arquivo de 3 bytes não pode fazer o servidor
 * subir com o modelo vazio.
 */
@SpringBootApplication
@ConfigurationPropertiesScan
public class DashboardApplication {

    private static final Path MODELO_IDX = Paths.get("data", "processed", "modelo.idx");

    public static void main(String[] args) {
        Integer codigo = new DashboardApplication().executar(args);
        if (codigo != null) System.exit(codigo);
    }

    /** Devolve o código de saída, ou null quando o servidor web assumiu o processo. */
    private Integer executar(String[] args) {
        CliOptions opts;
        try {
            opts = CliOptions.parse(args);
        } catch (CliOptions.Erro e) {
            System.err.println(e.getMessage());
            if (e.mostrarUso()) uso(System.err);
            return e.codigo();
        }

        if (opts.ajuda()) {
            uso(System.out);
            return 0;
        }

        // Precedência do main.c: amostra > atualizar > geo-info.
        if (opts.amostra()) {
            try {
                Amostra.gravar(MODELO_IDX);
                System.out.printf("índice de amostra gravado em %s (3 municípios, 3 anos)%n", MODELO_IDX);
                return 0;
            } catch (IOException e) {
                System.err.printf("não consegui gravar o índice de amostra em %s%n", MODELO_IDX);
                return 1;
            }
        }

        if (opts.atualizar()) {
            return IngestaoCli.executar(opts.fonte());
        }

        if (opts.geoInfo()) {
            return GeoInfo.executar(Paths.get("data", "processed"));
        }

        Modelo modelo;
        try {
            modelo = LeitorIndice.ler(MODELO_IDX);
        } catch (IOException | RuntimeException e) {
            System.err.printf("Não há dado processado.%n"
                    + "Rode: ./dashboard --atualizar ibge%n"
                    + "(ele baixa a lista de municípios do IBGE e a série de "
                    + "população, e grava %s).%n", MODELO_IDX);
            return 1;
        }

        System.out.printf("Modelo: %d municípios, %d anos, %d UFs (%s)%n",
                modelo.nLocal(), modelo.nAnos(), modelo.nUfs(), MODELO_IDX);
        System.out.flush();

        SpringApplication app = new SpringApplication(DashboardApplication.class);
        app.setWebApplicationType(WebApplicationType.SERVLET);
        // Default properties perdem para o application.yml; argumentos de
        // linha de comando têm precedência e são o que a CLI promete.
        // O modelo é carregado uma vez, antes do Tomcat, e injetado como bean
        // imutável — o equivalente Java do modelo lido antes do fork no C.
        app.addInitializers(ctx -> ctx.getBeanFactory().registerSingleton("modelo", modelo));

        try {
            app.run("--server.port=" + opts.porta(),
                    "--server.address=127.0.0.1",
                    "--dashboard.web-root=" + opts.webRoot());
        } catch (Throwable t) {
            System.err.printf("não consegui servir na porta %d (já está em uso?)%n", opts.porta());
            return 1;
        }

        // O Tomcat já está no ar e mantém threads não-daemon; main pode sair.
        System.out.printf("Dashboard em http://127.0.0.1:%d/ (web: %s)%n",
                opts.porta(), opts.webRoot());
        System.out.flush();
        return null;
    }

    private static void uso(PrintStream out) {
        // O C imprime argv[0]; o contrato de uso é o wrapper ./dashboard.
        // A lista de fontes inclui cnes porque a migração a aceita; o C
        // anunciava só ibge|malhas e recusava a terceira apesar de tê-la.
        out.println("uso: ./dashboard [--porta N] [--web DIR] "
                + "[--atualizar [ibge|malhas|cnes]] [--geo-info] [--amostra]");
    }
}
