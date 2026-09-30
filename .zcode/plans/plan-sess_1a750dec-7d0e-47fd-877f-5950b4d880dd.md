# Plan: agregar stampvar, if, switch, for y fun a rocky.cpp

Con esto el lenguaje llega a sus 10 comandos (stamp, int, str, mix, stampvar, if, switch, for, fun, ret) — justo lo que pide el encabezado del archivo.

## Decisiones confirmadas contigo

- **switch**: se evalúan los condicionales en orden; se ejecuta el cuerpo del primero que sea verdadero y se sale (sin fallthrough). Soporta `default {...}`.
- **Funciones**: ámbito propio por llamada (parámetros + locales nuevos; pueden LEER las globales; la recursión funciona). `ret valor;` devuelve y la llamada se usa donde va un término: `int x = suma(2,3);`
- **for**: `for(i = 0 / i < 10 / i++)` — si `i` no existe se declara como int automáticamente; si existía se reutiliza. La 3ª parte acepta `var++`, `var--` y `var = término (+ término)`.
- **Condiciones completas**: `==`, `!=`, `<`, `>`, `<=`, `>=` combinadas con `&&` y `||`; una variable sola es verdadera si es ≠ 0; `else` y `else if` en el if; strings comparables con `==` y `!=` (con `<` etc. entre strings → error).

## Sintaxis resultante

```
stampvar("Hola {nombre}, tienes {edad} años");   // {var} -> valor (str o int)
if (x < 10 && y != 0) { ... } else { ... }
switch(x){                                        // lo de switch(...) se acepta y se ignora
    (x > 5)   { ... }                             // cada caso trae SU condicional completo
    (x == 3)  { ... }
    default   { ... }
}
for(i = 0 / i < 10 / i++){ ... }
fun int suma(int a, int b){ ret a + b; }
suma(2, 3);                                       // llamada como sentencia (void o ignorando retorno)
fun void saluda(){ stamp("hola"); }
```

## Cambios de arquitectura (obligatorios por los bloques)

El intérprete actual es línea-a-línea; if/for/switch/fun tienen cuerpos multilínea que se ejecutan 0..N veces, así que:

1. **Dos fases**: `main` lee TODO el programa a un `vector<pair<lineno, línea>>` y luego lo ejecuta con un cursor. Los bloques consumen su cuerpo del vector (soporta anidamiento y re-ejecutar el cuerpo en cada iteración). `execLine(lineno, línea)` —el bucle de reglas actual, extraído a función— y `runBlock(líneas)` se llaman recursivamente.
2. **Ámbitos**: `struct Scope { map<string,int> ints; map<string,string> strs; }` + `globals` y una pila `callStack` para llamadas. La búsqueda va de local a global; las declaraciones escriben en el ámbito actual (las funciones pueden sombrear globales). `mix` queda solo global. Las funciones `resolve*` pierden los parámetros de mapas y usan los helpers de búsqueda (menos ruido en cada llamada).
3. **Conteo de llaves** que ignora las que están dentro de comillas (para acumular cuerpos correctamente aunque haya `stampvar("{x}")` dentro).
4. **Errores**: igual que ahora — `Error (line N)` con la línea real de la sentencia (cada línea guarda su lineno) y el intérprete continúa. `ret` lanza una señal interna (`struct ReturnSignal`) que atrapa `callFunction`; no es un error. Llaves sin cerrar → error y se detiene.

## Nuevas piezas

- **Evaluadores compartidos**: `evalIntTerm`/`evalStrTerm` (reconocen literales, variables y ahora llamadas `name(args)`), `evalIntSum` (el `A + B` del int, extraído para reutilizarlo en el for), `splitArgs` (separa argumentos respetando comillas), `evalCondition` (`||` → `&&` → comparación → término solo).
- **callFunction**: valida que la función exista, el número y los tipos de argumentos (se evalúan en el ámbito del llamador ANTES de crear el frame), tope de recursión (512), ejecuta el cuerpo y valida el tipo del `ret`. Función sin `ret` siendo int/str → error.
- **Reglas nuevas**: `stampvar`, `ret`, y la regla de llamada-como-sentencia `nombre(args);` — colocada AL FINAL del vector de reglas para que no se coma a `stamp`/`int`/`str`/`mix` (que van primero).
- **Términos extendidos** a `[A-Za-z_]\w*(\s*\([^()]*\))?` en int, str, stamp y condiciones → permite `stamp(suma(1,2))`. Sin llamadas anidadas dentro de argumentos.

## Limitaciones (documentadas en comentarios del código)

- No hay asignación suelta `x = 5;` como comando (no estaba en tu lista); las actualizaciones de variables van en el for.
- `mix` no se pasa como parámetro ni se declara dentro de funciones.
- Sin paréntesis dentro de condiciones ni llamadas anidadas en argumentos.
- Un for con condición siempre verdadera se cuelga (igual que C++).

## Formato

Sigo tu estilo de código (raw strings `R"rx(...)rx"`, reglas en el vector, `throw std::runtime_error`, mismo brace style) y el formato de comentarios didácticos en español que ya tiene el archivo, actualizando/corrigiendo los comentarios que toque el refactor (incluido el encabezado, que ahora sí podrá listar los 10 comandos).

## Archivos

- **rocky.cpp** — todo lo anterior.
- **test2.rocky** (nuevo) — demo de los comandos nuevos: stampvar, if/else/else if, for, switch con default, funciones int/str/void con recursión (factorial), llamadas dentro de términos y un par de errores controlados. `test.rocky` queda intacto.

## Verificación

1. Compilar con `g++ -std=c++17 -Wall -Wextra` (cero warnings).
2. `./rocky < test.rocky` debe dar EXACTAMENTE la salida actual (Hello World!, errores en líneas 7-9, luego 5/15/25).
3. `./rocky < test2.rocky` con salidas esperadas verificadas a mano (factorial, interpolaciones, ramas tomadas/no tomadas).