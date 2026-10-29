package br.gov.dashboard.modelo;

import br.gov.dashboard.comum.Texto;

/**
 * Modelo em memória: localidades, anos e um inteiro por indicador.
 *
 * O C compartilha a estrutura entre processos filhos via {@code fork}; aqui o
 * bean é lido por várias threads do Tomcat. Por isso os arrays são privados,
 * copiados na construção e nunca devolvidos por referência — a imutabilidade
 * não é estilo, é o que substitui o fork.
 *
 * Ausência é {@code -1}, nunca 0 (model.h). O JSON converte para {@code null};
 * o domínio mantém a sentinela para não inventar valor onde a série tem lacuna.
 */
public final class Modelo {

    public static final int AUSENTE = -1;

    private final Localidade[] local;
    private final Uf[] ufs;
    private final int[] anos;
    private final int[] valores;

    public Modelo(Localidade[] local, Uf[] ufs, int[] anos, int[] valores) {
        this.local = local.clone();
        this.ufs = ufs.clone();
        this.anos = anos.clone();
        this.valores = valores.clone();
    }

    public int nLocal() {
        return local.length;
    }

    public int nUfs() {
        return ufs.length;
    }

    public int nAnos() {
        return anos.length;
    }

    public Localidade local(int i) {
        return local[i];
    }

    public Uf uf(int i) {
        return ufs[i];
    }

    public int ano(int i) {
        return anos[i];
    }

    public int[] anos() {
        return anos.clone();
    }

    public boolean vazio() {
        return local.length == 0 || anos.length == 0;
    }

    public int indiceAno(int ano) {
        for (int i = 0; i < anos.length; i++) {
            if (anos[i] == ano) return i;
        }
        return anos.length;
    }

    public int localDeCod(String cod) {
        if (cod == null || cod.isEmpty()) return local.length;
        for (int i = 0; i < local.length; i++) {
            if (local[i].cod().equals(cod)) return i;
        }
        return local.length;
    }

    /**
     * Sigla em qualquer caixa ou código de dois dígitos do IBGE. Uma letra só
     * não é UF; comparar os bytes da sigla com o código nunca casaria (11 ≠
     * "RO"), por isso o código é resolvido pelo prefixo de um município.
     */
    public int ufDeSigla(String uf) {
        if (uf == null || uf.length() < 2) return ufs.length;
        boolean codigo = uf.length() == 2
                && uf.charAt(0) >= '0' && uf.charAt(0) <= '9'
                && uf.charAt(1) >= '0' && uf.charAt(1) <= '9';

        for (int i = 0; i < ufs.length; i++) {
            if (codigo) {
                for (int j = 0; j < local.length; j++) {
                    if (!local[j].uf().equals(ufs[i].sigla())) continue;
                    String cod = local[j].cod();
                    if (cod.length() >= 2 && cod.charAt(0) == uf.charAt(0)
                            && cod.charAt(1) == uf.charAt(1)) {
                        return i;
                    }
                    break;
                }
                continue;
            }
            String sigla = ufs[i].sigla();
            if (sigla.length() >= 2
                    && Character.toUpperCase(sigla.charAt(0)) == Character.toUpperCase(uf.charAt(0))
                    && Character.toUpperCase(sigla.charAt(1)) == Character.toUpperCase(uf.charAt(1))) {
                return i;
            }
        }
        return ufs.length;
    }

    public int valor(Indicador ind, int iLocal, int ano) {
        if (valores == null || ind == null) return AUSENTE;
        if (iLocal < 0 || iLocal >= local.length) return AUSENTE;
        if (anos.length == 0) return AUSENTE;
        int k = indiceAno(ano);
        if (k >= anos.length) return AUSENTE;
        long passo = (long) local.length * anos.length;
        long base = (long) ind.codigo() * passo + (long) iLocal * anos.length + k;
        if (base < 0 || base >= valores.length) return AUSENTE;
        return valores[(int) base];
    }

    /** Só para testes e para o escritor; não exponha isto na API. */
    public int[] valoresCrus() {
        return valores.clone();
    }

    public Localidade[] localidades() {
        return local.clone();
    }

    public Uf[] ufsCrus() {
        return ufs.clone();
    }

    public static String nomeIndicador(Indicador ind) {
        return ind == null ? "desconhecido" : ind.nome();
    }

    /** Usado por testes para afirmar a ordenação por bytes, não por Unicode. */
    public static int compararNome(String a, String b) {
        return Texto.compararBytes(a, b);
    }
}
