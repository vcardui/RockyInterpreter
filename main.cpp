/*
9 de diciembre de 2024
Vanessa Reteguín - 375533

Título

Universidad Aútonoma de Aguascalientes
Ingeniería en Computación Inteligente (ICI)
Semestre III

Grupo 
Materia
Profesor

Instrucciones:

*/

/* ------------------------- Libraries ------------------------- */
#include <iostream> /* cin/cout */
using namespace std; 

/* ------------------------- Functions ------------------------- */
void diplayMenu() {
    cout << endl
         << "\n.----------------."
            "\n||  -{ MENU }-  ||"
            "\n.----------------."
            "\n| [1] Opción 1.  |"
            "\n| [2] Opción 2.  |"
            "\n|     [3] Salir  |"
            "\n.----------------.\n";
}

void endTitle() {
    cout << "\n  ^~^  , * ------------- *"
            "\n ('Y') ) |  Hasta luego! | "
            "\n /   \\/  * ------------- *"
            "\n(\\|||/)        FIN      \n";
}

int main() {
		/* ------------------------- Variables ------------------------- */
    /* - Menu - */
    int userChoice;
    bool run = true;

		/* --------------------------- Code ---------------------------- */
    while (run == true) {
        diplayMenu();
        while (!((cin >> userChoice) && (userChoice >= 1 && userChoice <= 3))) {
            cin.clear();
            cin.ignore();
        }

        switch (userChoice) {
            case 1:
                cout << endl << "Opción 1";
                break;

            case 2:
                cout << endl << "Opción 2";
                break;

            case 3:
                endTitle();
                run = false;
                break;
        }
    }

    return 0;
}
