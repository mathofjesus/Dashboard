package br.gov.dashboard.cli;

/**
 * Parser próprio, antes do Spring. O Spring trataria {@code --atualizar} como
 * uma property e o valor seguinte como outro argumento; o contrato da CLI é o
 * do main.c, com código de saída 0/1/2.
 */
public record CliOptions(
        int porta,
        String webRoot,
        boolean ajuda,
        boolean amostra,
        boolean geoInfo,
        boolean atualizar,
        String fonte) {

    public static final class Erro extends RuntimeException {
        private final int codigo;
        private final boolean mostrarUso;

        public Erro(String mensagem, int codigo) {
            this(mensagem, codigo, false);
        }

        public Erro(String mensagem, int codigo, boolean mostrarUso) {
            super(mensagem);
            this.codigo = codigo;
            this.mostrarUso = mostrarUso;
        }

        public int codigo() {
            return codigo;
        }

        public boolean mostrarUso() {
            return mostrarUso;
        }
    }

    public static CliOptions parse(String[] args) {
        int porta = 8080;
        String web = "web";
        boolean ajuda = false, amostra = false, geoInfo = false, atualizar = false;
        String fonte = null;

        for (int i = 0; i < args.length; i++) {
            String a = args[i];

            if (a.equals("--help") || a.equals("-h")) {
                ajuda = true;
            } else if (a.equals("--geo-info")) {
                geoInfo = true;
            } else if (a.equals("--amostra")) {
                amostra = true;
            } else if (a.equals("--porta")) {
                if (i + 1 >= args.length) throw new Erro("faltou o valor de --porta", 2);
                porta = parseInteiro(args[++i], "porta inválida: ");
            } else if (a.startsWith("--porta=")) {
                porta = parseInteiro(a.substring("--porta=".length()), "porta inválida: ");
            } else if (a.equals("--web")) {
                if (i + 1 >= args.length) throw new Erro("faltou o valor de --web", 2);
                web = args[++i];
            } else if (a.startsWith("--web=")) {
                web = a.substring("--web=".length());
            } else if (a.equals("--atualizar")) {
                atualizar = true;
                // Como no C, o argumento seguinte é a fonte mesmo que pareça
                // outra opção; --atualizar no fim vale "ibge".
                if (i + 1 < args.length) fonte = args[++i];
            } else if (a.startsWith("--atualizar=")) {
                atualizar = true;
                fonte = a.substring("--atualizar=".length());
                if (fonte.isEmpty()) {
                    throw new Erro("--atualizar sem fonte: use ibge, malhas ou cnes", 2);
                }
            } else {
                throw new Erro("opção desconhecida: " + a, 2, true);
            }
        }

        return new CliOptions(porta, web, ajuda, amostra, geoInfo, atualizar, fonte);
    }

    private static int parseInteiro(String valor, String prefixo) {
        try {
            int v = Integer.parseInt(valor);
            if (v < 1 || v > 65535) throw new NumberFormatException();
            return v;
        } catch (NumberFormatException e) {
            throw new Erro(prefixo + valor, 2);
        }
    }
}
