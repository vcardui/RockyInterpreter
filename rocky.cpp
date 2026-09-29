/*
9 de diciembre de 2024
Paola Montserrat Osorio García - 216511
Pablo David Pérez López - 300452
Vanessa Reteguín - 375533

Intérprete Rocky — los 10 comandos del lenguaje

Universidad Aútonoma de Aguascalientes
Ingeniería en Computación Inteligente (ICI)
Semestre VII

Grupo: A
Materia: Autómatas II
Profesor: Braulio Jesús Montoya Padilla

Instrucciones: Codificar los 10 comandos del intérprete que creamos
en clase [Rocky]:
    stamp, int, str, mix, int[,] (matriz), stampvar, if, switch, for, fun
(fun usa `ret` para devolver valores; `ret` no cuenta como comando).
24 de septiembre de 2026: se agregan matrix, stampvar, if, switch,
for, fun y ret al intérprete original.

*/

/* =====================================================================
 * IDEA GENERAL DEL INTÉRPRETE
 * =====================================================================
 * Rocky es un intérprete "dirigido por expresiones regulares":
 *
 *   1. (Fase 1) El programa se lee COMPLETO desde la entrada estándar
 *      a memoria: un vector `prog` de pares (número de línea, texto).
 *      Con `./rocky < test.rocky` el archivo entra por stdin.
 *
 *   2. (Fase 2) Un cursor recorre `prog`. Para cada línea:
 *        - Si es cabecera de bloque (if / else if / else / for /
 *          switch / fun), se localiza su cuerpo contando llaves y se
 *          ejecuta la lógica propia del comando (que puede lanzar el
 *          cuerpo 0, 1 o N veces).
 *        - Si es una línea simple, se compara contra la lista de
 *          REGLAS. Cada regla es un par (regex, acción): la regex
 *          RECONOCE el comando y extrae sus partes con grupos de
 *          captura, y la acción (una lambda) lo EJECUTA. Gana la
 *          primera regla cuya regex calce con TODA la línea.
 *
 *   3. Errores: igual que siempre, `Error (line N): ...` con la línea
 *      real de la sentencia, y el intérprete SIGUE con lo siguiente
 *      (no se detiene al primer error). Única excepción: llaves sin
 *      cerrar (FatalError), que sí detienen la ejecución porque el
 *      resto del archivo ya no tiene sentido.
 *
 * Memoria del intérprete (SCOPES):
 *   - `globals`  : el ámbito de "arriba", donde viven las variables
 *                  del programa principal y todos los mix y matrices.
 *   - `callStack`: cada llamada a función abre un ámbito NUEVO
 *                  (parámetros + variables declaradas dentro). Las
 *                  funciones LEEN las globales si el nombre no existe
 *                  localmente, pero sus declaraciones son locales:
 *                  por eso la recursión funciona. Al terminar la
 *                  llamada, el ámbito se destruye.
 *   - `functions`: las funciones declaradas con `fun` (su cuerpo se
 *                  guarda como rango de líneas de `prog`).
 * ===================================================================== */

/* ------------------------- Libraries ------------------------- */
#include <bits/stdc++.h> // atajo que incluye casi toda la STL: iostream, map, vector, function, sstream...
#include <cctype>        // std::isdigit: saber si un carácter es un dígito 0-9
#include <regex>         // std::regex: motor de expresiones regulares (el corazón del intérprete)
#include <stdexcept>     // std::runtime_error / std::out_of_range: excepciones que lanzamos ante errores
#include <string>        // std::string

/*
Para inicializar el código se debe tener un archivo `.rocky`
Para correr el test:
    g++ -std=c++17 -Wall -Wextra -o rocky rocky.cpp && ./rocky < test.rocky
    g++ -std=c++17 -Wall -Wextra -o rocky rocky.cpp && ./rocky < test2.rocky
*/

/* ---------------------------------------------------------------------
 * struct Rule: una "regla" del lenguaje = UN comando de una sola línea.
 *
 * El intérprete funciona probando reglas una por una contra cada línea,
 * así que cada comando simple (stamp, stampvar, int, str, mix, matriz,
 * ret y la llamada a función) se define como un objeto de este struct
 * con dos piezas:
 *
 *   - pattern: la expresión regular que la línea debe cumplir para
 *     reconocer el comando. Los paréntesis de la regex son "grupos de
 *     captura" que guardan las partes variables (nombre de variable,
 *     literal, tamaño...). Después del match, el grupo i-ésimo se lee
 *     como m[i] y m[i].matched dice si ese grupo participó.
 *
 *   - run: la acción a ejecutar cuando la regex calza. Su tipo
 *     std::function<void(const std::smatch&)> se lee así: "cualquier
 *     función/lambda que recibe el resultado del match (m) y no
 *     devuelve nada". Las acciones NO capturan nada ([ ]) porque todo
 *     el estado del intérprete ahora es global (ver "ESTADO" abajo).
 *
 * Los comandos de BLOQUE (if, switch, for, fun) NO son reglas: sus
 * líneas de cabecera se detectan antes, en execStatement, porque
 * necesitan "comerse" varias líneas de cuerpo.
 * --------------------------------------------------------------------- */
struct Rule{
    std::regex pattern;                          // qué líneas reconoce esta regla
    std::function<void(const std::smatch&)> run; // qué hacer con ellas (lambda)
};

/* =====================================================================
 * ESTADO (MEMORIA) DEL INTÉRPRETE
 * =====================================================================
 * Un "ámbito" (scope) es una pareja de mapas nombre -> valor, uno por
 * cada tipo primitivo del lenguaje. Como C++ no permite mapas con
 * tipos mezclados, se llevan separados. Los arreglos (mix y matrices)
 * quedan fuera de los scopes: solo existen en el ámbito global.
 * ===================================================================== */
struct Scope {
    std::map<std::string, int> ints;               // variables int del ámbito
    std::map<std::string, std::string> strs;       // variables str del ámbito
};

Scope globals;                                     // ámbito del programa principal
std::vector<Scope> callStack;                      // pila de ámbitos de las funciones activas (vacía = nivel superior)
std::map<std::string, std::vector<std::string>> mix;               // mix[2] v = [...]  -> mix["v"] = {"a", "b"}
std::map<std::string, std::vector<std::vector<int>>> matrices;     // int[2,3] m = [[..]] -> matrices["m"] = {{..},{..}}

const int MAX_CALL_DEPTH = 512;                    // tope de recursión: evita que un `fun` mal hecho reviente la pila de C++

/* Una función declarada con `fun` se guarda aquí. El cuerpo NO se
 * copia: se guardan los índices de `prog` donde empieza y termina. */
struct FunDef {
    std::string type;                                        // "int", "str" o "void"
    std::vector<std::pair<std::string, std::string>> params; // (tipo, nombre) de cada parámetro
    size_t bodyStart = 0;                                    // índice en prog de la 1ª línea del cuerpo
    size_t bodyEnd   = 0;                                    // índice en prog de la línea del '}' de cierre (exclusivo)
};
std::map<std::string, FunDef> functions;

/* El programa completo: pares (número de línea real, texto de la línea).
 * Guardar el número junto a la línea permite que los errores dentro de
 * un bloque digan la línea de archivo correcta. */
using Program = std::vector<std::pair<int, std::string>>;
Program prog;

/* Retorno de una llamada a función: el tipo ("int"/"str"/"void") más
 * el valor en el campo que corresponda. */
struct RetVal {
    std::string type;
    int i = 0;
    std::string s;
};

/* Un "término" ya evaluado, con su tipo. Lo usan las condiciones, que
 * pueden comparar ints o strs. */
struct TypedVal {
    bool isStr = false;
    int i = 0;
    std::string s;
};

/* Señal interna que lanza el comando `ret` para salir de una función.
 * NO hereda de std::exception a propósito: no es un error, es control
 * de flujo. La atrapa callFunction y la convierte en RetVal. */
struct ReturnSignal {
    bool hasValue = false;   // ¿venía con valor? (ret; vs ret algo;)
    bool isStr = false;
    int i = 0;
    std::string s;
};

/* Error ESTRUCTURAL (llaves sin cerrar). Sí hereda de std::exception,
 * pero main lo distingue para DETENER la ejecución en vez de seguir. */
struct FatalError : std::runtime_error {
    FatalError(const std::string& w) : std::runtime_error(w) {}
};

/* Adelantos necesarios: los términos pueden llamar funciones
 * (evalIntTerm -> callFunction), las funciones ejecutan bloques
 * (callFunction -> runRange -> execStatement) y las condiciones se
 * apoyan en evalComparison. C++ exige declarar antes de usar. */
RetVal callFunction(const std::string& name, const std::string& argsSrc);
void runRange(size_t from, size_t to);
void execStatement(size_t& i);
bool evalComparison(const std::string& text);

/* =====================================================================
 * REGEX GLOBALES
 * =====================================================================
 * Se compilan UNA vez al arrancar (compilar regex es caro). Los trozos
 * que se concatenan con + se arman así para reutilizar fragmentos, y
 * pasan por safeRe() para que, si una regex está mal escrita, el
 * error de compilación diga CUÁL fue.
 * ===================================================================== */

/* ---------------------------------------------------------------------
 * safeRe: compila una regex y, si lanza regex_error, reporta el
 * patrón culpable antes de re-lanzar. Sin esto, un paréntesis de más
 * en una regex larga es un crash ilegible al arrancar.
 * --------------------------------------------------------------------- */
std::regex safeRe(const std::string& pat, const char* label) {
    try { return std::regex(pat); }
    catch (const std::regex_error& e) {
        std::cerr << "regex_error in " << label << ": " << e.what()
                  << "\n  pattern = [" << pat << "]\n";
        throw;
    }
}

/* ---------------------------------------------------------------------
 * Piezas de la regex que valida los ELEMENTOS de un mix. Un elemento
 * puede ser UNO de tres alternados:
 *
 *      "[^"]*"     |     '[^']'     |     -?\d+
 *      \_____/           \_____/          \___/
 *   "texto" (o "")       'c' (un         123 ó -7
 *                        solo carácter)
 * --------------------------------------------------------------------- */
const std::string mixItemAlt = R"rx("[^"]*"|'[^']'|-?\d+)rx";
/* El mismo alternado envuelto entre paréntesis: al paréntesis se le
 * llama "self-balanced unit" porque forma UN grupo de captura completo
 * y reutilizable, que se puede pegar dentro de otras regex sin romper
 * el conteo de grupos. */
const std::string mixItem    = "(" + mixItemAlt + ")";
/* mixItem compilado. Se usa con sregex_iterator para RECORRER el
 * contenido de un mix extrayendo cada elemento uno a uno. (regex_match
 * exige que toda la cadena calce; el iterador encuentra TODAS las
 * coincidencias parciales dentro de una cadena larga.) */
const std::regex mixItemRe ( mixItem );
/* Regex que valida la LISTA COMPLETA de un mix. Equivale a:
 *
 *     ^ \s* ( ELEMENTO ( \s*,\s* ELEMENTO ) * ) ? \s* $
 *
 * Es decir: espacios opcionales, cero o más elementos separados por
 * comas (la lista vacía se permite gracias al '?'), espacios finales.
 * Sirve para rechazar listas mal formadas como [1, ,2] o [hola] */
const std::regex mixBlobRe ( R"rx(^\s*(?:)rx" + mixItem +
                             R"rx((?:\s*,\s*)rx" + mixItem +
                             R"rx()*)?\s*$)rx" );

/* --- matrix grammar: net paren count annotated per fragment --- */
const std::string intItem = R"rx(-?\d+)rx";                  //  0

const std::string intRow  = R"rx(\[\s*(?:)rx" + intItem +    // +1
                            R"rx((?:\s*,\s*)rx" + intItem +  // +1
                            R"rx()*)\s*\])rx";               // -2  → net 0 ✓

const std::regex intItemRe = safeRe(intItem, "intItemRe");
const std::regex intRowRe  = safeRe("(" + intRow + ")", "intRowRe");
const std::regex intBlobRe = safeRe(
    R"rx(^\s*(?:)rx" + intRow + R"rx((?:\s*,\s*)rx" + intRow +
    R"rx()*)?\s*$)rx", "intBlobRe");

/* ---------------------------------------------------------------------
 * Términos reutilizables (se pegan dentro de varias regex):
 *
 *   termFrag    : un TÉRMINO ENTERO = variable (que además puede ser
 *                 una LLAMADA a función `name(...)`), o literal entero
 *                 con '-' opcional. Sin llamadas anidadas: [^()]*
 *                 prohíbe paréntesis dentro del argumento.
 *   strTermFrag : un TÉRMINO STR = variable o llamada (sin literales
 *                 numéricos: `str s = 5` sigue siendo error de
 *                 sintaxis, como siempre).
 * --------------------------------------------------------------------- */
const std::string termFrag    = R"rx(([A-Za-z_]\w*(?:\s*\([^()]*\))?|-?\d+))rx";
const std::string strTermFrag = R"rx(([A-Za-z_]\w*(?:\s*\([^()]*\))?))rx";

/* Expresión entera aceptada por el init/update del for y por `ret`:
 * TÉRMINO (+ TÉRMINO)?   (grupos 1 y 2). */
const std::regex intSumRe = safeRe(
    R"rx(^\s*)rx" + termFrag +
    R"rx(\s*(?:\+\s*)rx" + termFrag +
    R"rx()?\s*$)rx", "intSumRe");

/* Detecta si un término es una LLAMADA a función: grupo 1 = nombre,
 * grupo 2 = texto crudo de los argumentos (puede venir vacío). */
const std::regex callRe = safeRe(R"rx(^\s*([A-Za-z_]\w*)\s*\(([^()]*)\)\s*$)rx", "callRe");

/* Un parámetro de `fun`:  int nombre   ó   str nombre */
const std::regex paramRe = safeRe(R"rx(^\s*(int|str)\s+([A-Za-z_]\w*)\s*$)rx", "paramRe");

/* Las llaves {nombre} que stampvar interpola dentro de su texto. */
const std::regex stampvarVarRe = safeRe(R"rx(\{([A-Za-z_]\w*)\})rx", "stampvarVarRe");

/* ---------------------------------------------------------------------
 * Cabeceras de BLOQUE: todas terminan en '{' (el cuerpo viene después)
 * y sus grupos guardan lo que va entre paréntesis. El '}' de cierre,
 * en cambio, va SOLO en su propia línea (igual que en los ejemplos
 * del lenguaje).
 * --------------------------------------------------------------------- */
const std::regex ifRe      ( R"rx(^\s*if\s*\((.*)\)\s*\{\s*$)rx" );        // g1 = condición
const std::regex elseRe    ( R"rx(^\s*else\s*\{\s*$)rx" );
const std::regex elseIfRe  ( R"rx(^\s*else\s+if\s*\((.*)\)\s*\{\s*$)rx" ); // g1 = condición
const std::regex forRe     ( R"rx(^\s*for\s*\(\s*(.+?)\s*/\s*(.+?)\s*/\s*(.+?)\s*\)\s*\{\s*$)rx" ); // g1=init, g2=cond, g3=update (separados por '/' en vez de ';')
const std::regex switchRe  ( R"rx(^\s*switch\s*\(.*\)\s*\{\s*$)rx" );      // lo de switch(...) se acepta y se ignora: cada caso trae SU condicional
const std::regex funRe     ( R"rx(^\s*fun\s+(int|str|void)\s+([A-Za-z_]\w*)\s*\((.*)\)\s*\{\s*$)rx" ); // g1=tipo, g2=nombre, g3=parámetros
const std::regex caseRe    ( R"rx(^\s*\((.+)\)\s*\{\s*$)rx" );             // caso de switch: g1 = su condicional
const std::regex defaultRe ( R"rx(^\s*default\s*\{\s*$)rx" );              // caso default de switch

/* Partes del for:  init/update = var = expr ;  incremento = var++ / var-- */
const std::regex forInitRe = safeRe(R"rx(^\s*([A-Za-z_]\w*)\s*=\s*(.+?)\s*$)rx", "forInitRe");
const std::regex forIncRe  = safeRe(R"rx(^\s*([A-Za-z_]\w*)\s*\+\+\s*$)rx", "forIncRe");
const std::regex forDecRe  = safeRe(R"rx(^\s*([A-Za-z_]\w*)\s*--\s*$)rx", "forDecRe");

/* =====================================================================
 * AYUDANTES DE TEXTO
 * ===================================================================== */

/* trim: quita espacios, tabuladores y saltos sobrantes a los lados. */
std::string trim(const std::string& s){
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/* ---------------------------------------------------------------------
 * splitArgs: parte el texto de argumentos de una llamada por las comas
 * de "nivel superior". Las comas que estén DENTRO de "..." o '...'
 * no cuentan (p. ej. f("a,b") recibe UN argumento). Devuelve cada
 * argumento ya recortado; una lista vacía produce 0 argumentos.
 * --------------------------------------------------------------------- */
std::vector<std::string> splitArgs(const std::string& src){
    std::vector<std::string> out;
    std::string cur;
    bool inD = false, inS = false;             // ¿estamos dentro de "..." o '...'?
    for (size_t k = 0; k < src.size(); ++k) {
        char c = src[k];
        if (inD) { cur += c; if (c == '"')  inD = false; continue; }
        if (inS) { cur += c; if (c == '\'') inS = false; continue; }
        if (c == '"')  { inD = true;  cur += c; continue; }
        if (c == '\'') { inS = true;  cur += c; continue; }
        if (c == ',') { out.push_back(trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    std::string last = trim(cur);
    if (!last.empty() || !out.empty()) out.push_back(last); // el último argumento (si hubo comas, también el vacío -> error)
    return out;
}

/* ---------------------------------------------------------------------
 * splitTop: igual idea que splitArgs, pero para partir una CONDICIÓN
 * por "&&" o "||" que estén fuera de comillas. Así `"a&&b" == s` no
 * se parte por el && que está dentro del string.
 * --------------------------------------------------------------------- */
std::vector<std::string> splitTop(const std::string& src, const std::string& sep){
    std::vector<std::string> out;
    std::string cur;
    bool inD = false, inS = false;
    for (size_t k = 0; k < src.size(); ++k) {
        char c = src[k];
        if (inD) { cur += c; if (c == '"')  inD = false; continue; }
        if (inS) { cur += c; if (c == '\'') inS = false; continue; }
        if (c == '"')  { inD = true;  cur += c; continue; }
        if (c == '\'') { inS = true;  cur += c; continue; }
        if (src.compare(k, sep.size(), sep) == 0) { out.push_back(trim(cur)); cur.clear(); k += sep.size() - 1; continue; }
        cur += c;
    }
    out.push_back(trim(cur));
    return out;
}

/* ---------------------------------------------------------------------
 * braceDelta: cuántas llaves abre/cierra una línea, IGNORANDO las que
 * estén dentro de "..." o '...'. Es lo que permite acumular el cuerpo
 * de un bloque aunque adentro haya un stampvar("{x}").
 * --------------------------------------------------------------------- */
int braceDelta(const std::string& s){
    int d = 0;
    bool inD = false, inS = false;
    for (size_t k = 0; k < s.size(); ++k) {
        char c = s[k];
        if (inD) { if (c == '"')  inD = false; continue; }
        if (inS) { if (c == '\'') inS = false; continue; }
        if (c == '"')  { inD = true;  continue; }
        if (c == '\'') { inS = true;  continue; }
        if (c == '{') ++d;
        else if (c == '}') --d;
    }
    return d;
}

/* report: imprime un error con el formato de siempre y su línea real. */
void report(int lineno, const std::string& msg){
    std::cerr << "Error (line " << lineno << "): " << msg << '\n';
}

/* =====================================================================
 * AYUDANTES DE ÁMBITO (SCOPES)
 * =====================================================================
 * Lectura: se busca primero en el ámbito de la función activa (tope de
 * la pila) y luego en las globales. Escritura/declaración: SIEMPRE en
 * el ámbito actual (tope de la pila, o globals si no hay función
 * activa). Así una función puede sombrear una global sin romperla.
 * ===================================================================== */
Scope& curScope(){
    return callStack.empty() ? globals : callStack.back();
}

bool isInt(const std::string& name){
    if (!callStack.empty() && callStack.back().ints.count(name)) return true;
    return globals.ints.count(name) != 0;
}

bool isStr(const std::string& name){
    if (!callStack.empty() && callStack.back().strs.count(name)) return true;
    return globals.strs.count(name) != 0;
}

int getInt(const std::string& name){
    if (!callStack.empty()) {
        auto it = callStack.back().ints.find(name);
        if (it != callStack.back().ints.end()) return it -> second;
    }
    auto it = globals.ints.find(name);
    if (it == globals.ints.end())
        throw std::runtime_error("undefined variable '" + name + "'");
    return it -> second;
}

std::string getStr(const std::string& name){
    if (!callStack.empty()) {
        auto it = callStack.back().strs.find(name);
        if (it != callStack.back().strs.end()) return it -> second;
    }
    auto it = globals.strs.find(name);
    if (it == globals.strs.end())
        throw std::runtime_error("undefined variable '" + name + "'");
    return it -> second;
}

void setInt(const std::string& name, int value){ curScope().ints[name] = value; }
void setStr(const std::string& name, const std::string& value){ curScope().strs[name] = value; }

/* exists: ¿el nombre ya está tomado (en el ámbito actual o en los
 * arreglos globales mix/matrices)? Lo usan las declaraciones que
 * deben rechazar redeclaraciones. */
bool exists(const std::string& name){
    return curScope().ints.count(name) || curScope().strs.count(name) ||
           mix.count(name) || matrices.count(name);
}

/* =====================================================================
 * EVALUADORES: de texto -> valor
 * =====================================================================
 * Son el "corazón semántico": las reglas y las condiciones solo
 * recogen TEXTO (grupos del match); estas funciones lo convierten a
 * valores reales buscando variables, convirtiendo literales y lanzando
 * llamadas a funciones.
 * ===================================================================== */

/* ---------------------------------------------------------------------
 * evalTyped: evalúa un término que puede ser de CUALQUIER tipo y
 * devuelve valor + tipo. Lo usan las condiciones.
 *   - "texto"  -> literal str (debe venir bien cerrado)
 *   - f(...)   -> llamada a función (según lo que retorne)
 *   - nombre   -> variable str o int, lo que exista
 * --------------------------------------------------------------------- */
TypedVal evalTyped(const std::string& tok){
    std::string t = trim(tok);
    if (!t.empty() && (std::isdigit((unsigned char)t[0]) || t[0] == '-')) { // ¿literal entero?
        try{
            return TypedVal{false, std::stoi(t), ""};
        }catch(const std::out_of_range&){
            throw std::runtime_error("value out of int range: " + t);
        }
    }
    if (!t.empty() && t[0] == '"') {
        if (t.size() < 2 || t.back() != '"')
            throw std::runtime_error("unterminated string: " + t);
        return TypedVal{true, 0, t.substr(1, t.size() - 2)};
    }
    std::smatch c;
    if (std::regex_match(t, c, callRe)) {              // ¿es una llamada f(...)?
        RetVal r = callFunction(c[1].str(), c[2].str());
        if (r.type == "str") return TypedVal{true, 0, r.s};
        if (r.type == "int") return TypedVal{false, r.i, ""};
        throw std::runtime_error("function '" + c[1].str() + "' returns nothing");
    }
    if (isStr(t)) return TypedVal{true, 0, getStr(t)};
    if (isInt(t)) return TypedVal{false, getInt(t), ""};
    throw std::runtime_error("undefined variable '" + t + "'");
}

/* ---------------------------------------------------------------------
 * evalIntTerm: convierte un término entero a su valor numérico.
 *   - literal ("5", "-3"): stoi lo convierte; si no cabe en un int,
 *     out_of_range se traduce a un mensaje más claro.
 *   - f(...): llamada que debe retornar int.
 *   - variable int; si es str -> error de tipos; si no existe ->
 *     "undefined variable".
 * --------------------------------------------------------------------- */
int evalIntTerm(const std::string& tok){
    std::string t = trim(tok);
    if (!t.empty() && (std::isdigit((unsigned char)t[0]) || t[0] == '-')) { // ¿empieza con dígito o '-'? -> literal
        try{
            return std::stoi(t);
        }catch(const std::out_of_range&){
            throw std::runtime_error("value out of int range: " + t);
        }
    }
    std::smatch c;
    if (std::regex_match(t, c, callRe)) {              // ¿es una llamada f(...)?
        RetVal r = callFunction(c[1].str(), c[2].str());
        if (r.type == "int") return r.i;
        if (r.type == "str") throw std::runtime_error("type mismatch: fun " + c[1].str() + " returns a str");
        throw std::runtime_error("function '" + c[1].str() + "' returns nothing");
    }
    if (isInt(t)) return getInt(t);
    if (isStr(t)) throw std::runtime_error("type mismatch: '" + t + "' holds a str");
    throw std::runtime_error("undefined variable '" + t + "'");
}

/* ---------------------------------------------------------------------
 * evalStrTerm: convierte un término que DEBE ser cadena.
 *   - "texto" -> literal (se le quitan las comillas)
 *   - f(...)  -> llamada que debe retornar str
 *   - variable str; si es int -> error de tipos (test.rocky línea 8);
 *     si no existe -> "undefined variable".
 * --------------------------------------------------------------------- */
std::string evalStrTerm(const std::string& tok){
    std::string t = trim(tok);
    if (!t.empty() && t[0] == '"') {
        if (t.size() < 2 || t.back() != '"')
            throw std::runtime_error("unterminated string: " + t);
        return t.substr(1, t.size() - 2);
    }
    std::smatch c;
    if (std::regex_match(t, c, callRe)) {              // ¿es una llamada f(...)?
        RetVal r = callFunction(c[1].str(), c[2].str());
        if (r.type == "str") return r.s;
        if (r.type == "int") throw std::runtime_error("type mismatch: fun " + c[1].str() + " returns an int");
        throw std::runtime_error("function '" + c[1].str() + "' returns nothing");
    }
    if (isStr(t)) return getStr(t);
    if (isInt(t)) throw std::runtime_error("type mismatch: '" + t + "' holds an int");
    throw std::runtime_error("undefined variable '" + t + "'");
}

/* ---------------------------------------------------------------------
 * evalStampTerm: término que va a IMPRIMIRSE con stamp. Es el más
 * permisivo: acepta literales, llamadas y variables de cualquier tipo
 * (los int se vuelven texto con std::to_string). Aquí no puede haber
 * error de tipos porque para IMPRIMIR valen ambos.
 * --------------------------------------------------------------------- */
std::string evalStampTerm(const std::string& tok){
    std::string t = trim(tok);
    if (!t.empty() && (std::isdigit((unsigned char)t[0]) || t[0] == '-')) { // literal entero suelto (stamp(5))
        try{
            return std::to_string(std::stoi(t));
        }catch(const std::out_of_range&){
            throw std::runtime_error("value out of int range: " + t);
        }
    }
    std::smatch c;
    if (std::regex_match(t, c, callRe)) {              // ¿es una llamada f(...)?
        RetVal r = callFunction(c[1].str(), c[2].str());
        if (r.type == "int") return std::to_string(r.i);
        if (r.type == "str") return r.s;
        throw std::runtime_error("function '" + c[1].str() + "' returns nothing");
    }
    if (isStr(t)) return getStr(t);
    if (isInt(t)) return std::to_string(getInt(t));
    throw std::runtime_error("undefined variable '" + t + "'");
}

/* ---------------------------------------------------------------------
 * evalIntSum: evalúa una EXPRESIÓN entera completa: término (+ término)?
 * (el mismo `A + B` que acepta el comando int, reutilizado por el init
 * y el update del for y por `ret`). La forma la valida intSumRe.
 * --------------------------------------------------------------------- */
int evalIntSum(const std::string& expr){
    std::smatch m;
    if (!std::regex_match(expr, m, intSumRe))
        throw std::runtime_error("invalid int expression: " + expr);
    int value = evalIntTerm(m[1].str());
    if (m[2].matched)                                  // hubo '+' -> resolver y sumar el 2º término
        value += evalIntTerm(m[2].str());
    return value;
}

/* ---------------------------------------------------------------------
 * evalCondition: evalúa una condición completa con precedencia de C++:
 *
 *     ||   (o)      -> lo más flojo, se evalúa al final
 *     &&   (y)      -> más fuerte que ||
 *     comparación   -> ==  !=  <  >  <=  >=
 *     término solo  -> verdadero si es un int != 0
 *
 * Estrategia: se parte el texto por "||" (fuera de comillas); cada
 * parte se parte por "&&"; cada pedacito se evalúa con
 * evalComparison. Basta con que UNA rama del || sea verdadera.
 * --------------------------------------------------------------------- */
bool evalCondition(const std::string& cond){
    for (const std::string& orPart : splitTop(cond, "||")) {
        bool okAnd = true;
        for (const std::string& andPart : splitTop(orPart, "&&")) {
            if (!evalComparison(andPart)) { okAnd = false; break; }
        }
        if (okAnd) return true;
    }
    return false;
}

/* ---------------------------------------------------------------------
 * evalComparison: evalúa UNA comparación (o un término solo).
 * Primero busca el operador fuera de comillas (probando los de dos
 * caracteres antes, para no confundir <= con <). Sin operador, el
 * término debe ser int y vale como su != 0. Con operador: si algún
 * lado es str, SOLO se permiten == y != (ambos lados str).
 * --------------------------------------------------------------------- */
bool evalComparison(const std::string& text){
    std::string t = trim(text);
    if (t.empty())
        throw std::runtime_error("empty condition");

    std::string op;                 // el operador encontrado ("" = no hay comparación)
    size_t opPos = 0;               // dónde está
    bool inD = false, inS = false;  // para saltar el contenido de "..." y '...'
    for (size_t k = 0; k < t.size(); ++k) {
        char c = t[k];
        if (inD) { if (c == '"')  inD = false; continue; }
        if (inS) { if (c == '\'') inS = false; continue; }
        if (c == '"')  { inD = true;  continue; }
        if (c == '\'') { inS = true;  continue; }
        std::string two = t.substr(k, 2);
        if (two == "==" || two == "!=" || two == "<=" || two == ">=") { op = two; opPos = k; break; }
        if (c == '<' || c == '>') { op = std::string(1, c); opPos = k; break; }
        if (c == '=')
            throw std::runtime_error("expected '==' in condition (a lone '=' does not compare)");
    }

    if (op.empty()) {                              // término solo: verdad si es un int distinto de 0
        TypedVal v = evalTyped(t);
        if (v.isStr)
            throw std::runtime_error("condition must be an int");
        return v.i != 0;
    }

    std::string L = trim(t.substr(0, opPos));                     // lado izquierdo
    std::string R = trim(t.substr(opPos + op.size()));            // lado derecho
    TypedVal a = evalTyped(L);
    TypedVal b = evalTyped(R);

    if (a.isStr || b.isStr) {                      // comparación de STRINGS
        if (op != "==" && op != "!=")
            throw std::runtime_error("strings only compare with == or !=");
        if (!a.isStr || !b.isStr)
            throw std::runtime_error("type mismatch: comparing str with int");
        bool eq = (a.s == b.s);
        return op == "==" ? eq : !eq;
    }

    if (op == "==") return a.i == b.i;             // comparación de ENTEROS
    if (op == "!=") return a.i != b.i;
    if (op == "<")  return a.i <  b.i;
    if (op == ">")  return a.i >  b.i;
    if (op == "<=") return a.i <= b.i;
    return a.i >= b.i;
}

/* =====================================================================
 * LLAMADAS A FUNCIÓN
 * =====================================================================
 * Es el punto donde se abre y cierra un ámbito local:
 *   1. La función debe existir y recibir la cantidad correcta de args.
 *   2. Los argumentos se EVALÚAN en el ámbito del LLAMADOR (antes de
 *      abrir el frame) y se revisa que su tipo coincida con el del
 *      parámetro declarado (int o str).
 *   3. Se abre un Scope nuevo en la pila y los parámetros se guardan
 *      ahí como variables normales.
 *   4. Se ejecuta el cuerpo. Un `ret` lanza ReturnSignal: aquí se
 *      atrapa, se valida contra el tipo declarado y se convierte en
 *      RetVal. Si el cuerpo termina sin ret y la función no es void,
 *      es error. Si el cuerpo lanza un error NORMAL, se saca el frame
 *      de la pila antes de propagarlo (catch (...)).
 * ===================================================================== */
RetVal callFunction(const std::string& name, const std::string& argsSrc){
    auto fd = functions.find(name);
    if (fd == functions.end())
        throw std::runtime_error("undefined function '" + name + "'");
    const FunDef& f = fd->second;

    std::vector<std::string> args = splitArgs(argsSrc);
    if (args.size() != f.params.size())
        throw std::runtime_error("function '" + name + "' expects " +
                                 std::to_string(f.params.size()) + " argument(s), got " +
                                 std::to_string(args.size()));
    if (callStack.size() >= MAX_CALL_DEPTH)
        throw std::runtime_error("recursion too deep (limit " + std::to_string(MAX_CALL_DEPTH) + ")");

    /* 1) evaluar los argumentos ANTES de abrir el frame del llamado */
    std::vector<TypedVal> vals;
    for (size_t k = 0; k < args.size(); ++k) {
        if (f.params[k].first == "int")
            vals.push_back(TypedVal{false, evalIntSum(args[k]), ""});
        else
            vals.push_back(TypedVal{true, 0, evalStrTerm(args[k])});
    }

    /* 2) frame nuevo + parámetros como variables del ámbito local */
    callStack.push_back(Scope{});
    for (size_t k = 0; k < vals.size(); ++k) {
        if (vals[k].isStr) setStr(f.params[k].second, vals[k].s);
        else               setInt(f.params[k].second, vals[k].i);
    }

    /* 3) ejecutar el cuerpo y convertir su ret en valor de retorno */
    try {
        runRange(f.bodyStart, f.bodyEnd);
    } catch (const ReturnSignal& r) {
        callStack.pop_back();
        if (f.type == "void") {
            if (r.hasValue)
                throw std::runtime_error("function '" + name + "' is void: ret cannot carry a value");
            return RetVal{"void", 0, ""};
        }
        if (!r.hasValue)
            throw std::runtime_error("function '" + name + "' must return a " + f.type);
        if (f.type == "int") {
            if (r.isStr)
                throw std::runtime_error("type mismatch: '" + name + "' must return an int");
            return RetVal{"int", r.i, ""};
        }
        if (!r.isStr)
            throw std::runtime_error("type mismatch: '" + name + "' must return a str");
        return RetVal{"str", 0, r.s};
    } catch (...) {
        callStack.pop_back();   // error normal en el cuerpo: no dejar el frame colgado en la pila
        throw;
    }
    callStack.pop_back();
    if (f.type != "void")
        throw std::runtime_error("function '" + name + "' must return a " + f.type);
    return RetVal{"void", 0, ""};      // void: la llamada se usó como sentencia y su valor no importa
}

/* =====================================================================
 * REGLAS (comandos de una sola línea)
 * =====================================================================
 * El ORDEN importa: cada línea se prueba en orden y gana la primera
 * regex que calce con TODA la línea. Por eso la regla de LLAMADA
 * `nombre(...)` va AL FINAL: si estuviera antes se comería a
 * stamp(...), mix[...] etc. (los comandos con palabra clave van
 * primero). La numeración sigue la lista de los 10 comandos.
 * ===================================================================== */
std::vector<Rule> rules = {

    /* ================= COMANDO 1: stamp =================
     * Reconoce:  stamp("texto");   -> imprime el literal
     *            stamp(variable);  -> imprime el valor de la variable
     *            stamp(f(...));    -> imprime lo que retorne la función
     *
     * Regex (conceptual):
     *  ^ \s* stamp \s* \( \s* ( "([^"]*)" | (TÉRMINO) ) \s* \) \s* ;? \s* $
     *                          \_ grupo 1 _/ \_ grupo 2 _/
     *   grupo 1 = texto literal entre comillas dobles
     *   grupo 2 = variable, llamada o entero suelto (stamp(5))
     *   El ';' final es opcional y se toleran espacios sobrantes.
     *
     * La lambda decide según qué grupo calzó: si m[1].matched, era un
     * literal y se imprime tal cual; si no, evalStampTerm lo resuelve
     * (str o int) antes de imprimirlo. */
    {   std::regex(R"rx(^\s*stamp\s*\(\s*(?:"([^"]*)"|)rx" + termFrag +
                   R"rx()\s*\)\s*;?\s*$)rx"),
        [](const std::smatch& m){
            if(m[1].matched)                              // calzó el grupo 1 -> era literal
                std::cout << m[1].str() << '\n';
            else                                          // si no -> variable, llamada o entero
                std::cout << evalStampTerm(m[2].str()) << '\n';
        }},

    /* ================= COMANDO 6: stampvar =================
     * Como stamp, pero el texto admite VARIABLES interpoladas entre
     * llaves:  stampvar("Hola {nombre}, tienes {edad} anos");
     *
     * Regex: solo captura el texto completo entre comillas (grupo 1).
     * La lambda luego recorre el texto con stampvarVarRe buscando
     * {identificador} y sustituye cada uno por el valor de la variable
     * (str tal cual, int convertido a texto). Si el nombre no existe
     * en ningún mapa -> error. Las llaves que no formen un
     * {identificador} se dejan tal cual. */
    {   std::regex(R"rx(^\s*stampvar\s*\(\s*"([^"]*)"\s*\)\s*;?\s*$)rx"),
        [](const std::smatch& m){
            const std::string tpl = m[1].str();
            std::string out;
            size_t last = 0;                              // hasta dónde ya copiamos del texto
            for(auto it = std::sregex_iterator(tpl.begin(), tpl.end(), stampvarVarRe);
                it != std::sregex_iterator(); ++it){
                std::smatch v = *it;
                out += tpl.substr(last, v.position() - last);     // texto entre llaves
                const std::string name = v[1].str();
                if(isStr(name))                    out += getStr(name);
                else if(isInt(name))               out += std::to_string(getInt(name));
                else throw std::runtime_error("undefined variable '" + name + "'");
                last = v.position() + v.length();
            }
            out += tpl.substr(last);                      // cola final (después de la última llave)
            std::cout << out << '\n';
        }},

    /* ================= COMANDO 2: int =================
     * Reconoce:  int x = 5;
     *            int y = x;        (copia de otra variable int)
     *            int z = x + 3;    (suma término + término)
     *            int t = f(...);   (retorno int de una función)
     *
     * Regex (conceptual):
     *  ^ \s* int \s+ (nombre) \s* = \s* (TÉRMINO) (?: \s* \+ \s* (TÉRMINO) )? \s* $
     *    \_ grupo 1 _/         \_ grupo 2 _/          \_ grupo 3 _/
     *   grupo 2 y 3 usan termFrag: variable, llamada o literal entero.
     *
     * La lambda: 1) rechaza redeclarar el nombre en el ámbito ACTUAL
     * (las funciones pueden sombrear globales); 2) resuelve el primer
     * término; 3) si calzó el grupo 3, resuelve y SUMA el segundo;
     * 4) guarda en el ámbito actual. */
    {   std::regex(R"rx(^\s*int\s+([A-Za-z_]\w*)\s*=\s*)rx" + termFrag +
                   R"rx((?:\s*\+\s*)rx" + termFrag +
                   R"rx()?\s*$)rx"),
        [](const std::smatch& m){
            const std::string name = m[1].str();
            if(curScope().ints.count(name) || curScope().strs.count(name))
                throw std::runtime_error("redeclaration of " + name);
            int value = evalIntTerm(m[2].str());
            if(m[3].matched)                              // hubo '+' -> resolver y sumar el 2º término
                value += evalIntTerm(m[3].str());
            curScope().ints[name] = value;
        }},

    /* ================= COMANDO 3: str =================
     * Reconoce:  str s = "hola";
     *            str t = s;        (copia de otra variable str)
     *            str u = f(...);   (retorno str de una función)
     *
     * Regex (conceptual):
     *  ^ \s* str \s+ (nombre) \s* = \s* (?: "([^"]*)" | (TÉRMINO STR) ) \s* $
     *    \_ grupo 1 _/          \_ grupo 2 _/   \_ grupo 3 _/
     *   grupo 2 = literal entre comillas dobles (puede ser vacío "")
     *   grupo 3 = variable o llamada (strTermFrag: sin números, así
     *             `str s = 5` sigue siendo error de sintaxis)
     *
     * La lambda: 1) rechaza redeclaraciones (revisa los DOS mapas del
     * ámbito); 2) usa el literal si calzó el grupo 2, o evalStrTerm en
     * caso contrario (que exige que exista como str: un int da error
     * de tipos); 3) guarda en el ámbito actual. */
    {   std::regex(R"rx(^\s*str\s+([A-Za-z_]\w*)\s*=\s*(?:"([^"]*)"|)rx" + strTermFrag +
                   R"rx()\s*$)rx"),
        [](const std::smatch& m){
            const std::string name = m[1].str();
            if(curScope().ints.count(name) || curScope().strs.count(name))
                throw std::runtime_error("redeclaration of " + name);
            if(m[2].matched)                              // calzó el literal -> usarlo tal cual
                curScope().strs[name] = m[2].str();
            else                                          // si no -> variable o llamada str
                curScope().strs[name] = evalStrTerm(m[3].str());
        }},

    /* ================= COMANDO 4: mix =================
     * Reconoce:  mix[3] v = ["a", 'b', -5];
     *            mix[] v = [];      (tamaño opcional; lista vacía ok)
     *
     * Regex (conceptual):
     *  ^ \s* mix \s* \[ \s* ([1-9]\d*)? \s* \] \s+ (nombre) \s* = \s* \[(.*)\] \s* ;? \s* $
     *                       \_ grupo 1 _/      \_ grupo 2 _/     \_ grupo 3 _/
     *   grupo 1 = tamaño declarado (opcional; [1-9]\d* => >= 1 y sin ceros a la izquierda)
     *   grupo 2 = nombre de la variable nueva
     *   grupo 3 = TODO lo que hay dentro de los corchetes cuadrados (el "blob")
     *
     * La lambda: 1) rechaza redeclaraciones con exists(); 2) valida el
     * formato del blob con mixBlobRe: elementos válidos separados por
     * comas o lista vacía; 3) con mixItemRe y sregex_iterator extrae
     * cada elemento y lo guarda en un vector<std::string> (se guardan
     * como TEXTO tal cual se escribieron; el tipo se decide al leerlos);
     * 4) si se declaró tamaño, debe coincidir con la cantidad real. */
    {   std::regex(R"rx(^\s*mix\s*\[\s*([1-9]\d*)?\s*\]\s+([A-Za-z_]\w*)\s*=\s*\[(.*)\]\s*;?\s*$)rx"),
        [](const std::smatch& m){
            const std::string name = m[2].str();
            if(exists(name))
                throw std::runtime_error("redeclaration of " + name);

            const std::string blob = m[3].str();          // la lista cruda, tal cual se escribió

            if(!std::regex_match(blob, mixBlobRe))        // ¿la lista tiene formato válido?
                throw std::runtime_error("mix: invalid element list: [" + blob + "]");

            std::vector<std::string> items;
            for(auto it = std::sregex_iterator(blob.begin(), blob.end(), mixItemRe);
                it != std::sregex_iterator(); ++it)       // visita cada elemento del blob
                items.push_back((*it)[1].str());          // grupo 1 = el elemento

            if(m[1].matched && std::stoi(m[1].str()) != (int)items.size())
                throw std::runtime_error("mix: declared " + m[1].str() + " spaces, got " + std::to_string(items.size()));

            mix[name] = items;
        }},

    /* ================= COMANDO 5: matriz  int[f,c] =================
     * Reconoce:  int[2,3] m = [[1,2,3],[4,5,6]];
     *            int[] m = [[1,2],[3,4]];   (dimensiones opcionales)
     *
     * Regex (conceptual):
     *  ^ \s* int \s* \[ \s* (?: (F) \s*,\s* (C) )? \s* \] \s+ (nombre) \s* = \s* \[(.*)\] \s* ;? \s* $
     *                            \_ g1 _/  \_ g2 _/    \_ grupo 3 _/   \_ grupo 4 _/
     *   grupo 1 y 2 = filas y columnas declaradas (juntas o ninguna;
     *                 [1-9]\d* => >= 1 y sin ceros a la izquierda)
     *   grupo 3 = nombre de la variable nueva
     *   grupo 4 = TODO el blob de renglones, p. ej. `[1,2], [3,4]`
     *
     * La gramática del blob vive en las regex intRow/intBlobRe: cada
     * RENGLÓN es [int, int, ...] (mínimo un elemento) y el blob es
     * cero o más renglones separados por comas.
     *
     * La lambda: 1) rechaza redeclaraciones con exists(); 2) valida el
     * blob con intBlobRe; 3) recorre los renglones con intRowRe y cada
     * celda con intItemRe (celdas: literales enteros con '-' opcional);
     * 4) exige matriz RECTANGULAR: todos los renglones del mismo largo;
     * 5) si se declararon dimensiones, deben coincidir con las reales;
     * 6) guarda en el mapa global `matrices`. */
    {   std::regex(R"rx(^\s*int\s*\[\s*(?:([1-9]\d*)\s*,\s*([1-9]\d*))?\s*\]\s+([A-Za-z_]\w*)\s*=\s*\[(.*)\]\s*;?\s*$)rx"),
        [](const std::smatch& m){
            const std::string name = m[3].str();
            if(exists(name))
                throw std::runtime_error("redeclaration of " + name);

            const std::string blob = m[4].str();
            if(!std::regex_match(blob, intBlobRe))
                throw std::runtime_error("matrix: invalid row list: [" + blob + "]");

            std::vector<std::vector<int>> rows;
            size_t cols = 0;
            for(auto r = std::sregex_iterator(blob.begin(), blob.end(), intRowRe);
                r != std::sregex_iterator(); ++r){
                    // OJO: r->str() regresa un string TEMPORAL; si el iterador
                    // interno se armara directo sobre él, apuntaría a memoria
                    // ya liberada. Se copia a un local antes de recorrerlo.
                    const std::string row = r -> str();
                    std::vector<int> cells;
                    for(auto n = std::sregex_iterator(row.begin(), row.end(), intItemRe);
                        n != std::sregex_iterator(); ++n)
                        cells.push_back(evalIntTerm(n -> str()));
                    if(cols == 0) cols = cells.size();    // el 1er renglón fija el ancho
                    else if(cells.size() != cols)
                        throw std::runtime_error(    "matrix: row " + std::to_string(rows.size() + 1) +
                                                    " has " + std::to_string(cells.size()) +
                                                    " elements, expected " + std::to_string(cols));
                    rows.push_back(std::move(cells));
            }

            if(m[1].matched){
                if( std::stoi(m[1].str()) != (int)rows.size() ||
                    std::stoi(m[2].str()) != (int)cols)
                    throw std::runtime_error("matrix: declared " + m[1].str() + "x" + m[2].str() +
                                     ", got " + std::to_string(rows.size()) + "x" +
                                     std::to_string(cols));
            }
            matrices[name] = std::move(rows);
        }},

    /* ================= ret (parte de COMANDO 10: fun) =================
     * Reconoce:  ret;            (solo en funciones void)
     *            ret a + b;      (devuelve una expresión int)
     *            ret s;          (devuelve una variable str)
     *            ret "texto";    (devuelve un literal str)
     *
     * La lambda NO devuelve: lanza ReturnSignal, la señal interna que
     * callFunction atrapa para obtener el valor y salir de la función.
     * Fuera de una función es error. El valor int se evalúa con
     * evalIntSum (permite sumas y llamadas); el str, con evalStrTerm. */
    {   std::regex(R"rx(^\s*ret\s*;\s*$|^\s*ret\s+(.+?)\s*;?\s*$)rx"),
        [](const std::smatch& m){
            if(callStack.empty())
                throw std::runtime_error("ret outside a function");
            ReturnSignal r;
            if(m[1].matched){
                std::string val = trim(m[1].str());
                r.hasValue = true;
                if((!val.empty() && val[0] == '"') || isStr(val)){  // ¿es un str?
                    r.isStr = true;
                    r.s = evalStrTerm(val);
                }else{                                              // si no, es una expresión int
                    r.i = evalIntSum(val);
                }
            }
            throw r;
        }},

    /* ================= llamada como sentencia (parte de COMANDO 10: fun) =================
     * Reconoce:  nombre(args);      (fun void o ignorando el retorno)
     *
     * Esta regla va AL FINAL del vector a propósito: su regex
     * `nombre(...)` calzaría con stamp(...) o mix[...] si estuviera
     * antes, y los comandos con palabra clave tienen prioridad.
     * Si el nombre no es una función definida -> error claro. */
    {   std::regex(R"rx(^\s*([A-Za-z_]\w*)\s*\((.*)\)\s*;?\s*$)rx"),
        [](const std::smatch& m){
            const std::string name = m[1].str();
            if(!functions.count(name))
                throw std::runtime_error("undefined function '" + name + "'");
            callFunction(name, m[2].str());               // el retorno (si hay) se descarta
        }},
};

/* ---------------------------------------------------------------------
 * execLine: el bucle de reglas de toda la vida, ahora en una función
 * para que los bloques puedan ejecutar líneas simples también. Prueba
 * las reglas en orden; la primera que calce con TODA la línea gana.
 * Si ninguna calce -> error de sintaxis.
 * --------------------------------------------------------------------- */
void execLine(const std::string& line){
    for (const auto& rule : rules) {
        std::smatch m;   // aquí regex_match deja los grupos capturados: m[1], m[2], ...
        if (std::regex_match(line, m, rule.pattern)) { rule.run(m); return; }
    }
    throw std::runtime_error("syntax error: " + line);
}

/* =====================================================================
 * BLOQUES: if / else, for, switch y fun
 * =====================================================================
 * Las cabeceras de bloque NO pasan por el bucle de reglas: sus líneas
 * terminan en '{' y necesitan "comerse" el cuerpo. El cuerpo nunca se
 * copia: todos los bloques viven dentro del vector global `prog` y se
 * ejecutan por RANGOS de índices (from inclusive, to exclusivo).
 * ===================================================================== */

/* ---------------------------------------------------------------------
 * findClosing: devuelve el índice de la línea que cierra el bloque cuya
 * cabecera está en prog[open]. Cuenta llaves línea a línea (con
 * braceDelta, que ignora las de dentro de comillas) hasta que el nivel
 * vuelve a 0. Si el archivo se acaba sin cerrar -> FatalError (error
 * estructural que DETIENE la ejecución).
 * --------------------------------------------------------------------- */
size_t findClosing(size_t open){
    int depth = 1;   // la cabecera ya abrió su '{'
    for (size_t j = open + 1; j < prog.size(); ++j) {
        depth += braceDelta(prog[j].second);
        if (depth <= 0) return j;
    }
    throw FatalError("unbalanced braces: the block opened on this line never closes");
}

/* ---------------------------------------------------------------------
 * runRange: ejecuta las líneas prog[from, to) con el mismo dispatcher
 * (execStatement). Es lo que corre el cuerpo de un if, una vuelta del
 * for, el caso de un switch o el cuerpo de una función. La recursión
 * entre runRange <-> execStatement es la que permite bloques anidados.
 * --------------------------------------------------------------------- */
void runRange(size_t from, size_t to){
    for (size_t k = from; k < to; ) execStatement(k);
}

/* ---------------------------------------------------------------------
 * applyForInit: la 1ª parte del for (  variable = algo  ).
 * Si la variable no existe como int, SE DECLARA en el ámbito actual;
 * si ya existía, se reutiliza (se le asigna el valor inicial). Si es
 * un str -> error de tipos. La expresión acepta término (+ término)?
 * igual que el comando int.
 * --------------------------------------------------------------------- */
void applyForInit(const std::string& src){
    std::smatch m;
    if(!std::regex_match(src, m, forInitRe))
        throw std::runtime_error("invalid for init: " + src);
    const std::string name = m[1].str();
    if(isStr(name))
        throw std::runtime_error("type mismatch: '" + name + "' holds a str");
    setInt(name, evalIntSum(m[2].str()));   // declara si es nueva, reasigna si ya existía
}

/* ---------------------------------------------------------------------
 * intRef: referencia a la variable int 'name' (local o global, lo que
 * se vea desde aquí). Error claro si no existe o si es str. La usan
 * el ++ y el -- del for.
 * --------------------------------------------------------------------- */
int& intRef(const std::string& name){
    if(isStr(name))
        throw std::runtime_error("type mismatch: '" + name + "' holds a str");
    if(!isInt(name))
        throw std::runtime_error("undefined variable '" + name + "'");
    if(!callStack.empty() && callStack.back().ints.count(name))
        return callStack.back().ints[name];
    return globals.ints[name];
}

/* ---------------------------------------------------------------------
 * applyForUpdate: la 3ª parte del for. Acepta tres formas:
 *   var++            / var--          (incremento / decremento en 1)
 *   var = expr       (reasignación completa)
 * La variable debe existir como int (la del init ya lo está).
 * --------------------------------------------------------------------- */
void applyForUpdate(const std::string& src){
    std::smatch m;
    if(std::regex_match(src, m, forIncRe)){ ++intRef(m[1].str()); return; }
    if(std::regex_match(src, m, forDecRe)){ --intRef(m[1].str()); return; }
    if(std::regex_match(src, m, forInitRe)){            // misma forma que el init: var = expr
        setInt(m[1].str(), evalIntSum(m[2].str()));
        return;
    }
    throw std::runtime_error("invalid for update: " + src);
}

/* ---------------------------------------------------------------------
 * execIf: ejecuta un if en prog[i] (m = match de su cabecera) y deja
 * en i el índice siguiente a TODA la cadena if / else if / else.
 *
 *   - Si la condición es verdadera corre su cuerpo (runRange).
 *   - Si la condición falla, se mira si la línea siguiente es un
 *     `else {` (se corre su cuerpo) o un `else if (...) {` (se procesa
 *     como otro if encadenado, recursivamente).
 *   - `taken` viaja por la cadena: si una rama ya corrió, las demás se
 *     CONSUMEN sin ejecutarse (como los elif de Python).
 *   - Los errores dentro del cuerpo se reportan y se salta el resto
 *     del bloque (el intérprete sigue con lo que viene después).
 * --------------------------------------------------------------------- */
void execIf(size_t& i, std::smatch& m, bool taken){
    size_t close = findClosing(i);       // FUERA del try: las llaves sin cerrar son error fatal
    size_t after = close + 1;
    if(!taken){
        try{
            taken = evalCondition(m[1].str());
            if(taken) runRange(i + 1, close);
        }catch(const std::exception& e){
            report(prog[i].first, e.what());
            taken = true;                // si el if falló, el else ya no corre
        }
    }
    if(after < prog.size()){
        std::smatch me;
        if(std::regex_match(prog[after].second, me, elseRe)){
            size_t close2 = findClosing(after);
            if(!taken){
                try{ runRange(after + 1, close2); }
                catch(const std::exception& e){ report(prog[after].first, e.what()); }
            }
            i = close2 + 1;
            return;
        }
        if(std::regex_match(prog[after].second, me, elseIfRe)){
            size_t j = after;
            execIf(j, me, taken);        // else if = otro if encadenado
            i = j;
            return;
        }
    }
    i = after;
}

/* ---------------------------------------------------------------------
 * execFor: for( init / condición / update ){ ... } — igual que el for
 * de C++ pero separando las tres partes con '/' en vez de ';'.
 *   - El init corre UNA vez (declara la variable si es nueva).
 *   - Mientras la condición sea verdadera: cuerpo y luego update.
 *   - Como en C++, una condición siempre verdadera se cuelga: es
 *     responsabilidad del programa.
 * --------------------------------------------------------------------- */
void execFor(size_t& i, std::smatch& m){
    size_t close = findClosing(i);
    try{
        applyForInit(m[1].str());
        while(evalCondition(m[2].str())){
            runRange(i + 1, close);
            applyForUpdate(m[3].str());
        }
    }catch(const std::exception& e){
        report(prog[i].first, e.what());
    }
    i = close + 1;
}

/* ---------------------------------------------------------------------
 * execSwitch: casi como un switch de C++, pero sin "case n": cada caso
 * es directamente una CONDICIÓN entre paréntesis (que puede ser solo
 * una variable). Se recolectan los casos del cuerpo ( (cond){...} y
 * default {...} ) y luego:
 *   - se evalúan EN ORDEN y corre el cuerpo del PRIMERO verdadero
 *     (sin fallthrough, se sale del switch);
 *   - si ninguno fue verdadero, corre el default (si existe);
 *   - las líneas sueltas entre casos se ignoran.
 * --------------------------------------------------------------------- */
void execSwitch(size_t& i){
    size_t close = findClosing(i);

    struct Case { std::string cond; bool isDefault; size_t start; size_t close; };
    std::vector<Case> cases;
    size_t j = i + 1;
    while(j < close){
        std::smatch mc;
        if(std::regex_match(prog[j].second, mc, caseRe)){
            size_t cclose = findClosing(j);
            cases.push_back({mc[1].str(), false, j + 1, cclose});
            j = cclose + 1;
        }else if(std::regex_match(prog[j].second, mc, defaultRe)){
            size_t cclose = findClosing(j);
            cases.push_back({"", true, j + 1, cclose});
            j = cclose + 1;
        }else{
            ++j;   // línea fuera de los casos: se ignora
        }
    }

    try{
        bool done = false;
        for(const Case& c : cases){
            if(c.isDefault) continue;
            if(evalCondition(c.cond)){ runRange(c.start, c.close); done = true; break; }
        }
        if(!done){
            for(const Case& c : cases){
                if(c.isDefault){ runRange(c.start, c.close); break; }
            }
        }
    }catch(const std::exception& e){
        report(prog[i].first, e.what());
    }
    i = close + 1;
}

/* ---------------------------------------------------------------------
 * execFun: DEFINIR una función. NO la ejecuta: valida su encabezado
 * (tipo de retorno int/str/void, nombre nuevo, parámetros `tipo nombre`
 * sin repetir) y guarda en `functions` el tipo, los parámetros y el
 * rango de prog donde vive el cuerpo. La ejecución ocurre en
 * callFunction, cuando alguien la llama (las funciones deben definirse
 * ANTES de usarse, como se lee el archivo de arriba hacia abajo).
 * --------------------------------------------------------------------- */
void execFun(size_t& i, std::smatch& m){
    size_t close = findClosing(i);
    size_t after = close + 1;
    try{
        FunDef f;
        f.type = m[1].str();
        for(const std::string& p : splitArgs(m[3].str())){
            std::smatch pm;
            if(!std::regex_match(p, pm, paramRe))
                throw std::runtime_error("invalid parameter '" + p + "' in fun " + m[2].str());
            for(const auto& q : f.params)
                if(q.second == pm[2].str())
                    throw std::runtime_error("repeated parameter '" + pm[2].str() + "' in fun " + m[2].str());
            f.params.push_back({pm[1].str(), pm[2].str()});
        }
        if(functions.count(m[2].str()))
            throw std::runtime_error("redeclaration of function " + m[2].str());
        f.bodyStart = i + 1;                 // el cuerpo es un rango de prog, no una copia
        f.bodyEnd   = close;
        functions[m[2].str()] = f;
    }catch(const std::exception& e){
        report(prog[i].first, e.what());
    }
    i = after;
}

/* ---------------------------------------------------------------------
 * execStatement: el DISPATCHER. Dada la posición i de prog, decide qué
 * es la línea y la ejecuta, dejando en i la posición siguiente:
 *   - línea en blanco          -> se ignora
 *   - cabecera de bloque       -> su ejecutor (if/for/switch/fun)
 *   - cualquier otra           -> el bucle de reglas (execLine)
 *
 * Manejo de errores: cada línea simple atrapa SUS errores aquí y
 * reporta con su número de línea real; los ejecutores de bloque hacen
 * lo propio con su bloque. Solo FatalError (llaves sin cerrar) se
 * escapa hacia main, que detiene todo.
 * --------------------------------------------------------------------- */
void execStatement(size_t& i){
    const std::string& line = prog[i].second;
    if(trim(line).empty()){ ++i; return; }               // líneas en blanco se ignoran

    std::smatch m;
    if(std::regex_match(line, m, ifRe))     { execIf(i, m, false); return; }
    if(std::regex_match(line, m, forRe))    { execFor(i, m);       return; }
    if(std::regex_match(line, m, switchRe)) { execSwitch(i);       return; }
    if(std::regex_match(line, m, funRe))    { execFun(i, m);       return; }

    try {
        execLine(line);
    } catch (const std::exception& e) {
        /* Se atrapa std::exception —la clase madre de todas las
         * excepciones estándar— para no dejar escapar ninguna, incluso
         * deslices internos como un índice de grupo equivocado. */
        report(prog[i].first, e.what());
    }
    ++i;
}

/* =====================================================================
 * MAIN: dos fases
 * =====================================================================
 * Fase 1: leer TODO el programa a `prog` (cada línea con su número).
 * Fase 2: ejecutar con un cursor. Si sube un FatalError (llaves sin
 * cerrar) se reporta y se DETIENE: lo que sigue ya no tiene sentido.
 * ===================================================================== */
int main() {
    /* Fase 1: lectura completa del programa */
    std::string line;
    int lineno = 0;
    while (std::getline(std::cin, line)) {
        ++lineno;                                        // la 1ª línea del archivo es "line 1"
        prog.push_back({lineno, line});
    }

    /* Fase 2: ejecución con cursor */
    size_t i = 0;
    while (i < prog.size()) {
        try {
            execStatement(i);
        } catch (const FatalError& e) {
            report(prog[i].first, e.what());
            break;                                       // llaves sin cerrar: mejor parar aquí
        }
    }

    return 0;
}
