package br.gov.dashboard.consulta;

import br.gov.dashboard.comum.ApiException;
import br.gov.dashboard.comum.Texto;
import br.gov.dashboard.modelo.Indicador;
import br.gov.dashboard.modelo.Localidade;
import br.gov.dashboard.modelo.Modelo;
import br.gov.dashboard.modelo.Uf;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * A tabela filtrada, os KPIs e as séries — o porte de aggregate.c.
 *
 * Três regras que vieram do C e valem aqui:
 * 1. o índice vem do modelo, nunca de aritmética de arrays paralelos;
 * 2. {@code total} é o do conjunto, {@code limite} só corta a tabela;
 * 3. ausente (-1) não conta em KPI nenhum e vira {@code null} no JSON.
 */
@Service
public class ConsultaService {

    public Resultado consultar(Modelo m, Indicador ind, boolean nivelUf, String uf, int ano,
                               String busca, String ordenar, int limite) {
        if (ind == null) throw new ApiException(400, "indicador inválido");
        if (m == null || m.nAnos() == 0) {
            throw new ApiException(400, "não há dado processado: rode ./dashboard --atualizar ibge");
        }
        if (m.indiceAno(ano) >= m.nAnos()) {
            throw new ApiException(400, "ano " + ano + " inexistente na série");
        }
        if (nivelUf && m.nUfs() == 0) {
            throw new ApiException(400, "o modelo não tem tabela de UFs");
        }

        List<Item> itens = nivelUf
                ? linhasPorUf(m, ind, uf, ano, busca)
                : linhasPorMunicipio(m, ind, uf, ano, busca);

        int linhas = 0, comDado = 0, min = 0, max = 0;
        long soma = 0;
        for (Item item : itens) {
            int v = item.linha().valor();
            linhas++;
            if (v == Modelo.AUSENTE) continue;
            if (comDado == 0) {
                min = v;
                max = v;
            } else {
                if (v < min) min = v;
                if (v > max) max = v;
            }
            comDado++;
            soma += v;
        }
        int media = comDado > 0 ? (int) (soma / comDado) : 0;

        if (itens.size() > 1) itens.sort(comparador(ordenar));

        int limiteEfetivo = (limite > 0 && itens.size() > limite) ? limite : itens.size();
        List<Linha> saida = new ArrayList<>(limiteEfetivo);
        for (int i = 0; i < limiteEfetivo; i++) saida.add(itens.get(i).linha());

        return new Resultado(saida, new Kpis(min, max, media, soma, comDado), linhas);
    }

    /* ------------------------------------------------------------------ */
    /* linhas                                                              */
    /* ------------------------------------------------------------------ */

    private List<Item> linhasPorMunicipio(Modelo m, Indicador ind, String uf, int ano, String busca) {
        List<Item> itens = new ArrayList<>();
        for (int i = 0; i < m.nLocal(); i++) {
            Localidade l = m.local(i);
            if (!municipioEntra(m, l, uf, busca)) continue;
            itens.add(new Item(itens.size(), new Linha(l.cod(), l.nome(), m.valor(ind, i, ano))));
        }
        return itens;
    }

    private List<Item> linhasPorUf(Modelo m, Indicador ind, String uf, int ano, String busca) {
        List<Item> itens = new ArrayList<>();
        for (int i = 0; i < m.nUfs(); i++) {
            Uf u = m.uf(i);
            if (!ufEntra(m, u, uf)) continue;
            if (busca != null && !busca.isEmpty()
                    && !Texto.contemIgnorandoCaixa(u.sigla(), busca)
                    && !Texto.contemIgnorandoCaixa(u.nome(), busca)) continue;

            long soma = 0;
            boolean tem = false;
            Localidade primeiro = null;
            for (int j = 0; j < m.nLocal(); j++) {
                Localidade l = m.local(j);
                if (!l.uf().equals(u.sigla())) continue;
                if (primeiro == null) primeiro = l;
                int v = m.valor(ind, j, ano);
                if (v == Modelo.AUSENTE) continue;
                soma += v;
                tem = true;
            }

            String cod;
            if (primeiro != null && primeiro.cod().length() >= 2) {
                cod = primeiro.cod().substring(0, 2);
            } else {
                cod = u.sigla().length() > 7 ? u.sigla().substring(0, 7) : u.sigla();
            }
            String nome = (u.nome() == null || u.nome().isEmpty()) ? u.sigla() : u.nome();
            int valor = !tem ? Modelo.AUSENTE : (soma > Integer.MAX_VALUE ? Modelo.AUSENTE : (int) soma);
            itens.add(new Item(itens.size(), new Linha(cod, nome, valor)));
        }
        return itens;
    }

    /* ------------------------------------------------------------------ */
    /* filtros                                                             */
    /* ------------------------------------------------------------------ */

    private boolean municipioEntra(Modelo m, Localidade l, String uf, String busca) {
        if (l.cod() == null || l.cod().isEmpty()) return false;
        if (uf != null && !uf.isEmpty()) {
            int pos = m.ufDeSigla(uf);
            String nomeUf = pos < m.nUfs() ? m.uf(pos).nome() : null;
            if (!ufCasa(l, nomeUf, uf)) return false;
        }
        if (busca != null && !busca.isEmpty()
                && !Texto.contemIgnorandoCaixa(l.nome(), busca)
                && !Texto.contemIgnorandoCaixa(l.cod(), busca)) return false;
        return true;
    }

    /** A UF tem três grafias: sigla em qualquer caixa, código de 2 dígitos ou nome. */
    private boolean ufCasa(Localidade l, String nomeUf, String filtro) {
        if (filtro == null || filtro.isEmpty()) return true;
        if (l.uf() == null || l.uf().isEmpty()) return false;
        int n = filtro.length();
        if (n < 2) return false;
        if (n == 2 && Character.isDigit(filtro.charAt(0)) && Character.isDigit(filtro.charAt(1))) {
            return l.cod().length() >= 2
                    && l.cod().charAt(0) == filtro.charAt(0)
                    && l.cod().charAt(1) == filtro.charAt(1);
        }
        if (n == 2) {
            return Texto.minusculo(l.uf().charAt(0)) == Texto.minusculo(filtro.charAt(0))
                    && Texto.minusculo(l.uf().charAt(1)) == Texto.minusculo(filtro.charAt(1));
        }
        return nomeUf != null && Texto.contemIgnorandoCaixa(nomeUf, filtro);
    }

    private boolean ufEntra(Modelo m, Uf u, String filtro) {
        if (filtro == null || filtro.isEmpty()) return true;
        int pos = m.ufDeSigla(filtro);
        if (pos < m.nUfs()) return m.uf(pos).sigla().equals(u.sigla());
        return Texto.contemIgnorandoCaixa(u.nome(), filtro)
                || Texto.contemIgnorandoCaixa(u.sigla(), filtro);
    }

    /* ------------------------------------------------------------------ */
    /* ordenação                                                           */
    /* ------------------------------------------------------------------ */

    private enum Modo { VALOR_DESC, VALOR_ASC, NOME_ASC }

    private Modo modoDe(String ordenar) {
        if (ordenar != null) {
            if (ordenar.equals("valor_asc")) return Modo.VALOR_ASC;
            if (ordenar.equals("nome_asc")) return Modo.NOME_ASC;
        }
        return Modo.VALOR_DESC;
    }

    private Comparator<Item> comparador(String ordenar) {
        Modo modo = modoDe(ordenar);
        return (a, b) -> {
            if (modo == Modo.NOME_ASC) {
                int c = Texto.compararBytes(a.linha().nome(), b.linha().nome());
                if (c != 0) return c;
            } else {
                int va = a.linha().valor();
                int vb = b.linha().valor();
                if (va == Modelo.AUSENTE && vb != Modelo.AUSENTE) return 1;
                if (vb == Modelo.AUSENTE && va != Modelo.AUSENTE) return -1;
                if (va != vb) {
                    return (modo == Modo.VALOR_ASC) ? Integer.compare(va, vb) : Integer.compare(vb, va);
                }
            }
            return Integer.compare(a.ordem(), b.ordem());
        };
    }

    private record Item(int ordem, Linha linha) {
    }

    /* ------------------------------------------------------------------ */
    /* séries                                                              */
    /* ------------------------------------------------------------------ */

    /** Série de um município com {@code null} nos anos sem dado, ou null se o código não existe. */
    public List<Integer> serieMunicipio(Modelo m, Indicador ind, String cod) {
        int pos = m.localDeCod(cod);
        if (pos >= m.nLocal()) return null;
        List<Integer> serie = new ArrayList<>(m.nAnos());
        for (int i = 0; i < m.nAnos(); i++) {
            int v = m.valor(ind, pos, m.ano(i));
            serie.add(v == Modelo.AUSENTE ? null : v);
        }
        return serie;
    }

    /** Série somada por UF, ou null se a UF não existe no modelo. */
    public List<Integer> serieUf(Modelo m, Indicador ind, String uf) {
        int pos = m.ufDeSigla(uf);
        if (pos >= m.nUfs()) return null;
        String sigla = m.uf(pos).sigla();
        List<Integer> serie = new ArrayList<>(m.nAnos());
        for (int i = 0; i < m.nAnos(); i++) {
            long soma = 0;
            boolean tem = false;
            for (int j = 0; j < m.nLocal(); j++) {
                if (!m.local(j).uf().equals(sigla)) continue;
                int v = m.valor(ind, j, m.ano(i));
                if (v == Modelo.AUSENTE) continue;
                soma += v;
                tem = true;
            }
            serie.add(tem ? (soma > Integer.MAX_VALUE ? null : (int) soma) : null);
        }
        return serie;
    }

    /* ------------------------------------------------------------------ */
    /* opções                                                              */
    /* ------------------------------------------------------------------ */

    public List<Integer> anos(Modelo m) {
        List<Integer> anos = new ArrayList<>(m.nAnos());
        for (int i = 0; i < m.nAnos(); i++) anos.add(m.ano(i));
        return anos;
    }

    public List<String> siglasUf(Modelo m) {
        List<String> ufs = new ArrayList<>(m.nUfs());
        for (int i = 0; i < m.nUfs(); i++) ufs.add(m.uf(i).sigla());
        return ufs;
    }
}
