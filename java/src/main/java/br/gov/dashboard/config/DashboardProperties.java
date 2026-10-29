package br.gov.dashboard.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Configuração externalizada do dashboard. Os caminhos continuam relativos ao
 * diretório de execução, como no C — o binário/jar é rodado da raiz.
 */
@ConfigurationProperties(prefix = "dashboard")
public record DashboardProperties(String dataDir, String webRoot) {

    public DashboardProperties {
        if (dataDir == null || dataDir.isBlank()) dataDir = "data";
        if (webRoot == null || webRoot.isBlank()) webRoot = "web";
    }

    public Path processedDir() {
        return Paths.get(dataDir, "processed");
    }

    public Path modeloIdx() {
        return processedDir().resolve("modelo.idx");
    }

    public Path geoEstados() {
        return processedDir().resolve("geo_estados.json");
    }

    public Path geoMunicipios() {
        return processedDir().resolve("geo_municipios.json");
    }
}
