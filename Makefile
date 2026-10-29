# Makefile — build e suítes do dashboard (backend Java + front sem build).
#
# O contrato de fora é o mesmo de antes da migração: `make` deixa `./dashboard`
# pronto para uso e `make test` não depende de rede. O que mudou é o que roda
# por baixo: Maven no lugar do gcc, e as suítes C foram substituídas pelos
# testes JUnit equivalentes.

JAVA ?= java
MVN  ?= mvn
JAR   = java/target/dashboard.jar
FONTES_JAVA = $(shell find java/src -type f -name '*.java' 2>/dev/null) \
              java/pom.xml

# O primeiro alvo do arquivo é checa-ambiente (para rodar antes de tudo), mas o
# alvo padrão continua sendo all: `make` sem argumento tem de compilar.
.DEFAULT_GOAL := all

# O ambiente é verificado em TODA invocação de all/build/test — inclusive quando
# o JAR já existe e nada seria recompilado. Antes, `make build` numa máquina sem
# JDK/Maven dizia "Nada a ser feito" e o problema só aparecia no `java -jar`.
# A checagem do Java usado pelo próprio Maven pega o caso de `java` ser 21 e
# JAVA_HOME apontar para um JDK antigo.
checa-ambiente:
	@falha() { printf 'make: %s\n' "$$*" >&2; exit 1; }; \
	command -v "$(JAVA)" >/dev/null 2>&1 || \
	    falha "java não encontrado — instale um JDK 21+ ou defina JAVA=/caminho/para/java"; \
	v=$$("$(JAVA)" -version 2>&1 | awk -F'"' '/version/ {print $$2; exit}'); \
	major=$${v%%.*}; \
	if [ "$$major" = "1" ]; then major=$$(printf '%s' "$$v" | cut -d. -f2); fi; \
	case "$$major" in \
	    ''|*[!0-9]*) falha "não consegui determinar a versão do Java (saída: $${v:-vazia})" ;; \
	esac; \
	[ "$$major" -ge 21 ] || \
	    falha "é preciso Java 21+ (encontrado: $$v) — confira JAVA/JAVA_HOME"; \
	command -v "$(MVN)" >/dev/null 2>&1 || \
	    falha "maven não encontrado — instale Maven 3.9+ ou defina MVN=/caminho/para/mvn"; \
	saida=$$("$(MVN)" -v 2>&1) || falha "falha ao executar '$(MVN) -v': $$saida"; \
	v=$$(printf '%s\n' "$$saida" | sed -n 's/^Java version: \([0-9][^, ]*\).*/\1/p' | head -1); \
	major=$${v%%.*}; \
	if [ "$$major" = "1" ]; then major=$$(printf '%s' "$$v" | cut -d. -f2); fi; \
	case "$$major" in \
	    ''|*[!0-9]*) falha "não consegui ler o Java usado pelo Maven ('$(MVN) -v')" ;; \
	esac; \
	[ "$$major" -ge 21 ] || \
	    falha "o Maven está usando Java $$v; é preciso Java 21+ (confira JAVA_HOME)"

all: checa-ambiente $(JAR)

$(JAR): $(FONTES_JAVA)
	$(MVN) -q -f java/pom.xml -DskipTests package

build: all

# O front não tem build step (é ES modules servido como estático), então só há
# o que verificar: sintaxe de cada módulo e o porte de node antes do teste.
#
# `node` nem sempre está no PATH — no Debian ele costuma vir só de um
# gerenciador de versões instalado pelo usuário. Sem isto, `make test` morria em
# "node: not found" depois de a suíte já ter passado, o que é o pior lugar
# para descobrir que falta um interpretador. A ordem de busca é: o NODE do
# ambiente, o PATH, e então o node do próprio Hermes. `command -v` em vez de
# `which` porque `which` não é padrão em POSIX e aqui é o /bin/sh do dash.
NODE ?= $(shell command -v node 2>/dev/null || \
                 ls -d $${HOME}/.hermes/tools/node-*/bin/node 2>/dev/null | sort -V | tail -1)
web/test-web:
	@if [ -z "$(NODE)" ] || [ ! -x "$(NODE)" ]; then \
	    echo "make test: node não encontrado — defina NODE=/caminho/para/node" >&2; \
	    exit 1; \
	fi
	@for m in web/*.js; do $(NODE) --check "$$m" || exit 1; done
	@echo "sintaxe ok: $$(ls web/*.js | tr '\n' ' ')"

test: checa-ambiente $(JAR) web/test-web
	$(MVN) -q -f java/pom.xml test
	$(NODE) tests/test_web.mjs
	$(NODE) tests/test_map.js
	sh tests/test_web.sh

# `make clean` não passa pelo Maven de propósito: precisa funcionar justamente
# quando o ambiente está quebrado ou pela metade. java/target é o único diretório
# que o build cria.
clean:
	rm -rf java/target

.PHONY: all build test clean web/test-web checa-ambiente
