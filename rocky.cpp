/*
9 de diciembre de 2024
Paola Montserrat Osorio García - 
Pablo David Pérez López - 300452
Vanessa Reteguín - 375533

Título

Universidad Aútonoma de Aguascalientes
Ingeniería en Computación Inteligente (ICI)
Semestre III

Grupo: A
Materia: Autómatas II
Profesor: Braulio Jesús Montoya Padilla

Instrucciones: Codificar los 10 comandos del intérprete que creamos
en clase [Rocky]

*/

/* ------------------------- Libraries ------------------------- */
#include <bits/stdc++.h>
#include <cctype>
#include <stdexcept>
#include <string>

/*
Para inicializar el código se debe tener un archivo `.rocky`
Para correr el test:
    g++ -std=c++17 -Wall -Wextra -o rocky rocky.cpp && ./rocky < test.rocky
*/

struct Rule{
    std::regex pattern;
    std::function<void(const std::smatch&)> run;
};

int resolveTerm(const std::string& tok, const std::map<std::string, int>& vars){
    if(std::isdigit((unsigned char)tok[0]) || tok[0] == '-'){
        try{
            return std::stoi(tok);
        }catch(const std::out_of_range&){
            throw std::runtime_error("value out of int range: " + tok);
        }
    }

    auto it = vars.find(tok);
    if(it == vars.end())
        throw std::runtime_error("undefined variable '" + tok + "'");
    return it -> second;
}

std::string resolveStrTerm( const std::string tok,
                            const std::map<std::string, std::string>& strs,
                            const std::map<std::string, int>& ints){
    auto it = strs.find(tok);
    if(it != strs.end()) return it ->second;
    if(ints.count(tok))
        throw std::runtime_error("type mismatch: '" + tok + "' holds an int");
    throw std::runtime_error("undefinied variable '" + tok + "'");
}

int main() {
    std::map<std::string, int> variables;
    std::map<std::string, std::string> strVariables;

    std::vector<Rule> rules = {
        // stamp("text")
        {   std::regex(R"rx(^\s*stamp\("([^"]*)"\)\s*$)rx"),
            [](const std::smatch& m) {std::cout << m[1].str() << '\n';}},

        // int variable = dígito && int variable = variable + dígito && int variable = variable
        {   std::regex(R"rx(^\s*int\s+([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*|-?\d+)(?:\s*\+\s*([A-Za-z_]\w*|-?\d+))?\s*$)rx"),
            [&variables](const std::smatch& m){
                const std::string name = m[1].str();

                if(variables.count(name))
                    throw std::runtime_error("redeclaration of " + name);

                int value = resolveTerm(m[2].str(), variables);
                if(m[3].matched)
                    value += resolveTerm(m[3].str(), variables);

                variables[name] = value;
            }},

        // str string = "string"
        {   std::regex(R"rx(^\s*str\s+([A-Za-z_]\w*)\s*=\s*(?:"([^"]*)"|([A-Za-z_]\w*))\s*$)rx"),
            [&](const std::smatch& m){
                const std::string name = m[1].str();

                if(variables.count(name) || strVariables.count(name))
                    throw std::runtime_error("redeclaration of " + name);
                
                std::string value;
                if(m[2].matched)
                    value = m[2].str();
                else
                    value = resolveStrTerm(m[3].str(), strVariables, variables);

                strVariables[name] = value;
            }}
    };

    std::string line;
    int lineno = 0;
    while (std::getline(std::cin, line)) {
        ++lineno;
        try {
            bool ok = false;
            for (const auto& rule : rules) {
                std::smatch m;
                if (std::regex_match(line, m, rule.pattern)) { rule.run(m); ok = true; break; }
            }
            if (!ok) throw std::runtime_error("syntax error: " + line);
        } catch (const std::exception& e) {          // std::exception: also nets slips like bad group indexes
            std::cerr << "Error (line " << lineno << "): " << e.what() << '\n';
        }
    }

    return 0;
}
