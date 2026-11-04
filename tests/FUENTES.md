# Programas de prueba

Los 8 programas de la demostración son código abierto real y conocido, no escrito por nosotros.
Cada archivo conserva su encabezado de copyright y licencia originales.

## `validos/` (sin errores de sintaxis, el programa no debe imprimir nada)

| Archivo | Líneas | Descripción | Autor | Origen |
|---|---|---|---|---|
| `cJSON.c` (+ `cJSON.h`) | 3206 | Parser de JSON | Dave Gamble | https://github.com/DaveGamble/cJSON |
| `linenoise.c` (+ `linenoise.h`) | 2380 | Librería de edición de línea (readline minimalista) | Salvatore Sanfilippo y Pieter Noordhuis | https://github.com/antirez/linenoise 
| `ini.c` (+ `ini.h`) | 334 | Parser de archivos INI (inih) | Ben Hoyt | https://github.com/benhoyt/inih |
| `sha256.c` (+ `sha256.h`) | 158 | Algoritmo SHA-256 | Brad Conte | https://github.com/B-Con/crypto-algorithms |

Estos cuatro se usan sin modificar

## `con_errores/` (varios errores de sintaxis cada uno)

Son copias de programas conocidos a los que **se les inyectaron errores a propósito**. Fuera de las
líneas indicadas abajo, el código es idéntico al original.

| Archivo | Líneas | Descripción | Autor | Origen |
|---|---|---|---|---|
| `aes.c` (+ `aes.h`) | 572 | tiny-AES-c (AES en C) | kokke | https://github.com/kokke/tiny-AES-c |
| `re.c` (+ `re.h`) | 528 | tiny-regex-c (motor de expresiones regulares) | kokke | https://github.com/kokke/tiny-regex-c |
| `md5.c` (+ `md5.h`) | 189 | crypto-algorithms: MD5 | Brad Conte | https://github.com/B-Con/crypto-algorithms |
| `map.c` (+ `map.h`) | 193 | map (tabla hash en C) | rxi | https://github.com/rxi/map |

### Errores inyectados

**`aes.c`**

| Línea | Tipo | Cambio |
|---|---|---|
| 96 | falta `;` | `0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16 }` |
| 352 | falta `;` | `int i` |
| 428 | falta `)` | `if (round == Nr {` |
| 494 | falta `;` | `uint8_t i` |
| 504 | operador sin operando | `uint8_t *Iv = ctx->Iv + ;` |

**`re.c`**

| Línea | Tipo | Cambio |
|---|---|---|
| 75 | falta `;` | `return re_matchp(re_compile(pattern), text, matchlength)` |
| 140 | falta `)` | `if (pattern[i+1] != '\0'` |
| 203 | `return` mal escrito | `retrun 0;` |
| 378 | falta `;` | `while (*str++ != '\0')` |
| 403 | operador sin operando | `const char* prepoint = text + ;` |
| 430 | falta `)` | `if (matchpattern(pattern, text--, matchlength)` |

**`md5.c`**

| Línea | Tipo | Cambio |
|---|---|---|
| 45 | falta `;` | `a = ctx->state[0]` |
| 126 | operador sin operando | `ctx->datalen = 0 + ;` |
| 170 | falta `;` | `ctx->bitlen += ctx->datalen * 8` |

**`map.c`**

| Línea | Tipo | Cambio |
|---|---|---|
| 22 | falta `;` | `unsigned hash = 5381` |
| 76 | falta `)` | `if (buckets != NULL {` |
| 102 | `return` mal escrito | `retrun next;` |
| 173 | operador sin operando | `iter.bucketidx = -1 + ;` |

## Cómo correrlos

```bash
./cparser tests/validos/cJSON.c          # sin salida, código de salida 0
./cparser tests/con_errores/aes.c        # lista los errores con su línea y un ^
```
