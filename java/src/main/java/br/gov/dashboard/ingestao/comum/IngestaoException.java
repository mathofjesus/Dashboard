package br.gov.dashboard.ingestao.comum;

/** Falha de ingestão com mensagem já pronta para o terminal, em português. */
public class IngestaoException extends RuntimeException {

    public IngestaoException(String mensagem) {
        super(mensagem);
    }

    public IngestaoException(String mensagem, Throwable causa) {
        super(mensagem, causa);
    }
}
