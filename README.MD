# Parser de C con Flex y Bison

Analizador sintáctico para el lenguaje C, escrito en C sobre Linux con `flex` y `bison`. Recibe un programa fuente (aparentemente escrito en lenguaje C) y reporta **todos los errores sintácticos y léxicos** que encuentra, al estilo de gcc: archivo, línea, columna, mensaje, la línea de código y un indicador `^~~~` en el lugar del error. Si el programa no tiene errores, no imprime nada.

Proyecto del curso **Compiladores e Intérpretes**, Instituto Tecnológico de Costa Rica (TEC).

---

## Características

- **Preprocesador propio**: elimina comentarios, expande `#define` (con y sin parámetros), resuelve `#include` con detección de ciclos, y soporta compilación condicional (`#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`), `#undef` y `#pragma once`.
- **Scanner en Flex** con función `Get_Token()`, que entrega el tipo, el lexema, la línea, la columna y el largo de cada token.
- **Parser en Bison** para el lenguaje C completo (incluye elementos de C99).
- **Posiciones sobre el fuente original**: aunque el parser trabaja con el texto ya preprocesado, los errores se señalan en el archivo y la línea que escribió el usuario, incluso dentro de archivos incluidos.
- **Mensajes de error en español**, con lo esperado y lo encontrado, y con la posición más útil para corregir.
- **Recuperación y reparación de errores**: el parser sigue después de un error para reportar los siguientes, y repara los `;` faltantes cuando es seguro hacerlo.
- **Typedefs**: una tabla de símbolos mínima le permite al parser distinguir nombres de tipo de identificadores comunes.

---

## Requisitos

Linux con las herramientas de compilación estándar, Flex y Bison.

### Ubuntu / Debian / Linux Mint

```bash
sudo apt update
sudo apt install -y build-essential flex bison
```

---

## Compilación

```bash
make clean
make
```

Esto genera el ejecutable `./cparser`. Bison y Flex generan sus archivos dentro de `build/`.

---

## Uso

```bash
./cparser <archivo-fuente>
./cparser -h
```

El archivo fuente puede tener cualquier extensión. Si no se dan argumentos, o se dan de forma incorrecta, se muestra la ayuda.

### Código de salida

| Código | Significado |
|---|---|
| `0` | No se encontraron errores |
| `1` | Se encontraron errores, o el uso o el archivo de entrada son incorrectos |

### Ejemplo de salida

```
$ ./cparser tests/md5.c
tests/md5.c:50:23: error: ...
   50 |     a = ctx->state[0]
      |                      ^
```

Los errores se escriben en `stderr`. Si es una terminal, el indicador sale en color.

---

## Arquitectura

```
archivo.c
   │
   ▼
Preprocesador      quita comentarios, expande macros, resuelve #include y #if,
   │               y guarda de dónde vino cada línea y columna
   ▼
yyparse()          parser generado por Bison, pide tokens con yylex()
   │
   ▼
yylex()            adaptador (yylex_bridge.c): llama a Get_Token(), reporta
   │               errores léxicos y decide si un identificador es un typedef
   ▼
Get_Token()        scanner generado por Flex (scanner.l)
```

Cuando Bison detecta un error, llama a `yyerror()`, que pasa por `errors.c` (arma el mensaje en español y elige la posición) y por `diagnostics.c` (imprime con el formato de gcc usando el mapa de posiciones del preprocesador).

### Dónde se usa `Get_Token()`

- Se **define** en `src/scanner.l` y se **declara** en `src/scanner.h`.
- Se **llama** desde `yylex()` en `src/yylex_bridge.c`.
- También lo usa la herramienta de desarrollo `tools/tokdump.c`.

---

## Estructura del repositorio

```
.
├── Makefile
├── README.md
├── src/
│   ├── main.c              punto de entrada, opciones y validación del archivo
│   ├── preprocessor.c/.h   preprocesador y mapa de posiciones originales
│   ├── builtin_headers.c/.h  versiones mínimas de los headers estándar
│   ├── scanner.l           reglas de Flex y Get_Token()
│   ├── scanner.h           interfaz pública del scanner
│   ├── token.c/.h          estructura Token
│   ├── parser.y            gramática de C en Bison
│   ├── yylex_bridge.c      adaptador entre Flex y Bison
│   ├── lexer_state.h       interfaz entre el adaptador y los mensajes de error
│   ├── symtab.c/.h         tabla de símbolos para typedefs
│   ├── errors.c/.h         traducción y posicionamiento de los mensajes
│   ├── diagnostics.c/.h    impresión estilo gcc
│   └── source.c/.h         carga de archivos fuente para mostrar las líneas
├── tests/                  programas de prueba y run.sh
├── tools/                  herramientas de desarrollo
└── build/                  archivos generados (no se versionan)
```

---

## Detalles de diseño

### Tokens

Cada token lleva un código generado por Bison (`KW_INT`, `PLUS`, `LPAREN`, `DEC_LITERAL`, etc.), el lexema, la línea, la columna y el largo. Se usan unos 100 códigos distintos, porque la gramática necesita distinguir cada operador y cada palabra reservada. Los errores léxicos también tienen sus propios códigos.

### Typedefs

En C, una expresión como `T * x;` puede ser una multiplicación o una declaración de puntero, según si `T` es un tipo. Cuando el scanner entrega un identificador, `yylex_bridge.c` consulta la tabla de símbolos y, si es un typedef visible, lo convierte en `TYPE_NAME`. Cada bloque abre un scope, así que una variable local puede sombrear un typedef. Los nombres estándar más comunes (`size_t`, `FILE`, `uint8_t`...) están registrados desde el inicio.

### Posiciones sobre el fuente original

El preprocesador guarda, para cada byte de salida, la columna original de donde vino. El texto producido por una macro se asocia a la columna del nombre de la macro. Con ese mapa, `diagnostics.c` puede señalar el archivo, la línea y la columna correctas.

### Recuperación y reparación de errores

- **Reportar**: el mensaje dice lo esperado y lo encontrado, y apunta a la posición más útil.
- **Recuperar**: las reglas `error` de la gramática descartan tokens hasta un punto seguro (`;` o `}`) y el parser continúa.
- **Reparar**: cuando falta un `;` y el siguiente token solo puede empezar una nueva sentencia en una línea posterior, se entrega al parser un `;` virtual. Así se reporta un solo error y no se pierde código.

### Dangling else

La ambigüedad del `if` con y sin `else` se resuelve con `%precedence`: el `else` se asocia al `if` más cercano.

---

## Pruebas

```bash
./tests/run.sh
```

El script verifica que:

- los programas válidos no produzcan ninguna salida,
- los programas con errores terminen con código `1` y nunca con una caída (segmentation fault),
- un archivo con finales de línea CRLF se comporte igual que uno con LF.

En `tests/` hay reales de terceros, algunos válidos (`cJSON.c`, `sha256.c`, `ini.c`, `linenoise.c`) y otros con errores a propósito (`md5.c`, `map.c`, `re.c`, `aes.c`).

---

## Herramientas de desarrollo

No forman parte del programa final.

```bash
make tokdump && ./tokdump <archivo>    # lista los tokens que entrega Get_Token()
make ppdump  && ./ppdump <archivo>     # muestra el texto preprocesado y su origen
./ppdump <archivo> -c LINEA COL        # traduce una posición preprocesada a la original
```

---

## Limitaciones conocidas

- El preprocesador no soporta los operadores `#` (stringify) ni `##` (pegado de tokens) en macros.
- Los errores dentro del cuerpo de una macro expandida se señalan en el lugar de la llamada.
- Los headers del sistema son versiones mínimas, solo con los typedefs y macros que el parser necesita.
- No se reportan errores semánticos (variables sin declarar, tipos incompatibles), porque están fuera del alcance del proyecto.

---

## Autores

* **Luis Felipe Benavides Solórzano**
* **Matthew Gaviria Brenes**
* **Marvin Zamora Mussio**

**Institución:** Instituto Tecnológico de Costa Rica (TEC).
**Curso:** Compiladores e Intérpretes