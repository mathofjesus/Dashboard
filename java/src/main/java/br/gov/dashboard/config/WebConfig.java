package br.gov.dashboard.config;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.servlet.config.annotation.ResourceHandlerRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;

import java.nio.file.Path;
import java.nio.file.Paths;

/** Serve o front (web/) sem build step, exatamente como o servidor C. */
@Configuration
public class WebConfig implements WebMvcConfigurer {

    private final DashboardProperties props;

    public WebConfig(DashboardProperties props) {
        this.props = props;
    }

    @Override
    public void addViewControllers(org.springframework.web.servlet.config.annotation.ViewControllerRegistry registry) {
        // O C serve index.html na raiz; o ResourceHandler sozinho devolve 404
        // para "/" porque não existe um recurso com esse nome.
        registry.addViewController("/").setViewName("forward:/index.html");
    }

    @Override
    public void addResourceHandlers(ResourceHandlerRegistry registry) {
        Path raiz = Paths.get(props.webRoot()).toAbsolutePath().normalize();
        String uri = raiz.toUri().toString();
        if (!uri.endsWith("/")) uri += "/";
        registry.addResourceHandler("/**")
                .addResourceLocations(uri)
                .setCachePeriod(0);
    }
}
