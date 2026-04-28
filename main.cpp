#include "BTree.h"
#include <algorithm>
#include <vector>
#include <iostream>
#include <random>

using namespace std;

int main() {
    // rm test.db

    Disk disk("test.db");
    BPlusTree<int> tree(disk);

    const int N = 1000;

    // ================= INSERT RANDOM =================
    vector<int> nums;
    for (int i = 1; i <= N; i++) nums.push_back(i);

    random_device rd;
    mt19937 g(rd());
    shuffle(nums.begin(), nums.end(), g);

    for (int x : nums) {
        tree.insert(x, {x, 0});
    }

    cout << "Insercion random completada\n";

    // ================= SEARCH CHECK =================
    bool ok = true;
    for (int i = 1; i <= N; i++) {
        RID r = tree.search(i);
        if (isNullRID(r)) {
            cout << "❌ Error en key: " << i << endl;
            ok = false;
            break;
        }
    }

    if (ok) cout << "✅ Busqueda OK\n";

    // ================= RANGE TEST =================
    int a = 400, b = 420;
    auto res = tree.rangeSearch(a, b);

    cout << "Range [" << a << "," << b << "]: ";
    for (auto &r : res) {
        cout << r.page_id << " ";
    }
    cout << endl;

    // ================= DUPLICADOS =================
    cout << "\nTest duplicados:\n";
    tree.insert(500, {999,1});
    tree.insert(500, {999,2});

    auto dup = tree.searchAll(500);
    cout << "Cantidad de 500s: " << dup.size() << endl;

    // ================= DEBUG =================
    cout << "\nEstructura de hojas:\n";
    tree.printLeaves();

    return 0;
}