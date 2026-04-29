#include "BTree.h"
#include <algorithm>
#include <vector>
#include <iostream>
#include <random>
using namespace std;

int main() {
    // ⚠️ Borra si quieres empezar limpio
    // system("rm -f test.db");

    Disk disk("test.db");
    BPlusTree<int> tree(disk);

    const int INSERTS = 1000;
    const int MAX_KEY = 10000;

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dist(1, MAX_KEY);

    // ================= INSERT RANDOM DUPLICATES =================
    disk.resetCounters();

    for (int i = 0; i < INSERTS; i++) {
        int key = dist(gen);

        // RID único (simulado)
        RID rid = {key, i};  
        tree.insert(key, rid);
    }

    cout << "Insercion random (con duplicados) completada\n";
    cout << "Accesos a disco (insert): " 
         << disk.totalAccesses() << "\n\n";


    // ================= SEARCH RANDOM TEST =================
    disk.resetCounters();

    cout << "Busqueda de claves random:\n";
    for (int i = 0; i < 10; i++) {
        int key = dist(gen);

        auto results = tree.searchAll(key);

        cout << "Key " << key << " -> encontrados: " 
             << results.size() << endl;

        for (auto &r : results) {
            cout << "(" << r.page_id << "," << r.slot << ") ";
        }
        cout << "\n";
    }

    cout << "\nAccesos a disco (search): "
         << disk.totalAccesses() << "\n\n";


    // ================= RANGE TEST =================
    disk.resetCounters();

    int a = 100, b = 200;
    auto range = tree.rangeSearch(a, b);

    cout << "Range [" << a << "," << b << "]\n";
    cout << "Cantidad obtenida: " << range.size() << "\n";

    cout << "Primeros 20 resultados:\n";
    for (int i = 0; i < min(20, (int)range.size()); i++) {
        cout << "(" << range[i].page_id << "," 
             << range[i].slot << ") ";
    }
    cout << "\n";

    cout << "Accesos a disco (range): "
         << disk.totalAccesses() << "\n\n";


    // ================= RANGE GRANDE =================
    auto bigRange = tree.rangeSearch(1, 10000);
    cout << "Range total size: " << bigRange.size() << endl;
    cout << "Esperado aprox: " << INSERTS << "\n\n";


    // ================= TEST DUPLICADOS =================
    int testKey = dist(gen);

    auto dups = tree.searchAll(testKey);

    cout << "Test duplicados clave " << testKey << ":\n";
    cout << "Cantidad: " << dups.size() << "\n";

    for (auto &r : dups) {
        cout << "(" << r.page_id << "," << r.slot << ") ";
    }
    cout << "\n\n";


    // ================= PRINT =================
    cout << "Estructura de hojas:\n";
    tree.printLeaves();

    return 0;
}