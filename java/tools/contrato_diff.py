"""Compara a resposta do servidor C com a do servidor Java.

Uso: python3 contrato_diff.py http://127.0.0.1:18081 http://127.0.0.1:18082
Sai 0 quando status e JSON são iguais em todas as rotas; 1 caso contrário.
"""
import json
import sys
import urllib.error
import urllib.request

C, J = sys.argv[1], sys.argv[2]

ROTAS = [
    "/api/opcoes",
    "/api/opcoes?indicador=populacao",
    "/api/opcoes?indicador=popula%C3%A7%C3%A3o",
    "/api/opcoes?indicador=xyz",
    "/api/opcoes?uf=RO",
    "/api/opcoes?uf=11",
    "/api/opcoes?uf=SP",
    "/api/consulta?ano=2021",
    "/api/consulta?ano=2021&indicador=populacao",
    "/api/consulta?indicador=populacao",
    "/api/consulta?ano=",
    "/api/consulta?ano=20a4",
    "/api/consulta?ano=2020",
    "/api/consulta?ano=2021&indicador=xyz",
    "/api/consulta?ano=2021&nivel=uf",
    "/api/consulta?ano=2021&nivel=uf&uf=RO",
    "/api/consulta?ano=2021&nivel=uf&uf=11",
    "/api/consulta?ano=2021&nivel=uf&cod=1100015",
    "/api/consulta?ano=2021&cod=1100015",
    "/api/consulta?ano=2021&cod=9999999",
    "/api/consulta?ano=2021&busca=rio",
    "/api/consulta?ano=2021&busca=RIO",
    "/api/consulta?ano=2021&busca=1100",
    "/api/consulta?ano=2021&ordenar=nome_asc",
    "/api/consulta?ano=2021&ordenar=valor_asc",
    "/api/consulta?ano=2021&limite=2",
    "/api/consulta?ano=2021&uf=RO",
    "/api/consulta?ano=2021&uf=11",
    "/api/consulta?ano=2021&uf=Rond%C3%B4nia",
    "/api/consulta?ano=2021&uf=SP",
    "/api/consulta?ano=2021&fonte=ibge&limite=1&ordenar=nome_asc",
    "/api/consulta?ano=2021&nivel=municipio",
    "/api/consulta?ano=2021&nivel=xyz",
    "/api/valores?ano=2021",
    "/api/valores?ano=2021&nomes=1",
    "/api/valores?ano=2021&nomes=true",
    "/api/valores?ano=2021&nomes=0",
    "/api/valores?ano=2021&nivel=uf",
    "/api/valores?ano=2021&nivel=uf&nomes=1",
    "/api/valores?indicador=populacao",
    "/api/valores?ano=2021&uf=RO",
    "/api/geo/estados",
    "/api/geo/municipios",
    "/api/geo/municipios?uf=RO",
    "/api/geo/estados?uf=RO",
    "/api/geo/municipios?uf=XX",
    "/api/mapa/uf?uf=RO",
    "/api/mapa/uf?uf=RO&nivel=municipios",
    "/api/mapa/uf?uf=RO&nivel=estados",
    "/api/xyz",
]


def buscar(base, rota):
    try:
        with urllib.request.urlopen(base + rota, timeout=60) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


def main():
    falhas = 0
    for rota in ROTAS:
        sc, corpo_c = buscar(C, rota)
        sj, corpo_j = buscar(J, rota)
        if sc != sj or corpo_c != corpo_j:
            falhas += 1
            print(f"DIFERENTE {rota}  status C={sc} J={sj}")
            print("  C:", json.dumps(corpo_c, ensure_ascii=False, sort_keys=True)[:600])
            print("  J:", json.dumps(corpo_j, ensure_ascii=False, sort_keys=True)[:600])
    print(f"{len(ROTAS) - falhas}/{len(ROTAS)} rotas iguais")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
