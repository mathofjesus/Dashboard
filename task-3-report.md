# Tarefa 3: Transcodificação ISO-8859-1 → UTF-8

## Arquivos
- src/charset.h (novo)
- src/charset.c (novo)
- tests/test_charset.c (novo)
- tests/test_main.c (modificado)

## Contrato
int charset_latin1_para_utf8(const char *in, size_t in_len, char *out, size_t out_cap, size_t *out_len);
- 0 OK, 1 buffer pequeno (out_len = bytes necessários), 2 inválido.

## Execução
make test passa; charset testado.

