#include "BTree.h"
#include <iostream>
#include <vector>
#include <cassert>
using namespace std;

int main() {
    // ⚠️ borra test.db antes de correr si quieres empezar limpio
    // system("rm test.db");

    Disk disk("test.db");
    BPlusTree<char> tree(disk);

    cout << "===== INSERTANDO DATOS =====\n";

    // 🔥 insertar duplicados: 'A'
    for (int i = 0; i < 1000; i++) {
        tree.insert('A', {i, i});
    }

    // 🔥 insertar duplicados: 'B'
    for (int i = 0; i < 200; i++) {
        tree.insert('B', {i, i});
    }

    tree.printLeaves();

    // ================= SEARCH NORMAL =================
    cout << "\n===== SEARCH NORMAL =====\n";
    RID r = tree.search('A');

    if (!isNullRID(r)) {
        cout << "Encontrado A -> (" << r.page_id << ", " << r.slot << ")\n";
    } else {
        cout << "No encontrado A\n";
    }

    // ================= SEARCH ALL =================
    cout << "\n===== SEARCH ALL (A) =====\n";
    vector<RID> allA = tree.searchAll('A');
    cout << "Total encontrados (A): " << allA.size() << "\n";

    allA = tree.searchAll('B');
    cout << "Total encontrados (B): " << allA.size() << "\n";

    // ================= DELETE =================
    cout << "\n===== ELIMINANDO key = A =====\n";

    int before = tree.searchAll('A').size();
    cout << "Antes: " << before << "\n";

    tree.remove('A');
    tree.remove('B');

    int after = tree.searchAll('A').size();
    cout << "Despues: " << after << "\n";

    before = tree.searchAll('B').size();
    cout << "Antes: " << before << "\n";

    tree.printLeaves();

    // ================= VALIDACIÓN =================
    cout << "\n===== VALIDACIONES =====\n";

    assert(after == 0);
    cout << "Eliminacion correcta\n";

    cout << "\n===== FIN =====\n";

    tree.printLeaves();

    return 0;
}