package br.gov.dashboard.comum;

/** Erro de contrato da API: status HTTP e mensagem em português, como no C. */
public class ApiException extends RuntimeException {

    private final int status;

    public ApiException(int status, String mensagem) {
        super(mensagem);
        this.status = status;
    }

    public int status() {
        return status;
    }
}
