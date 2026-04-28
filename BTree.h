#pragma once
#include <bits/stdc++.h>
using namespace std;

// ===================== CONFIG =====================
static constexpr int PAGE_SIZE = 4096;

// RID: referencia al registro en el archivo externo (sequential file, etc.)
// El parser/visitor se encargará de conectar este índice con el archivo real.
struct RID {
    int  page_id;  // página en el archivo de datos
    int  slot;     // slot dentro de esa página
};

static constexpr RID NULL_RID = {-1, -1};

inline bool isNullRID(const RID& r) { return r.page_id == -1 && r.slot == -1; }

using PageID = int;
static constexpr PageID NULL_PAGE = -1;

// ===================== NODE LAYOUT =====================
// TKey: tipo de clave (int, float, string fija, etc.)
//       Debe ser trivialmente copiable y tener operator< y operator==.
//
// Calculamos MAX_KEYS en función de sizeof(TKey) para que
// sizeof(Node<TKey>) nunca supere PAGE_SIZE.
//
// Overhead fijo = sizeof(bool) + 2*sizeof(int) + sizeof(PageID)
//               = isLeaf + numKeys + parent + nextLeaf
// Por entrada hoja:     sizeof(TKey) + sizeof(RID)
// Por entrada interna:  sizeof(TKey) + sizeof(PageID)
//
// Usamos el caso más restrictivo (hoja) y restamos 1 de margen.

template<typename TKey>
static constexpr int computeMaxKeys() {
    int overhead = static_cast<int>(sizeof(bool) + 3 * sizeof(int) + sizeof(PageID));
    int leafEntry = static_cast<int>(sizeof(TKey) + sizeof(RID));
    return (PAGE_SIZE - overhead) / leafEntry - 1;
}

template<typename TKey>
struct Node {
    bool   isLeaf;
    int    numKeys;
    PageID parent;
    PageID nextLeaf;  // solo válido en hojas; en internos = NULL_PAGE

    static constexpr int MAX_KEYS = computeMaxKeys<TKey>();

    TKey keys[MAX_KEYS + 1];  // +1: buffer para inserción temporal antes del split

    union {
        PageID children[MAX_KEYS + 2];  // nodos internos  (+1 hijo extra)
        RID    values[MAX_KEYS + 1];    // nodos hoja
    };
};

struct Page {
    char data[PAGE_SIZE];
};

// ===================== DISK =====================
// Maneja lectura/escritura de páginas en disco.
// nextPage persiste usando los primeros 4 bytes del archivo (página de metadata).
class Disk {
private:
    fstream file;
    int     nextPage;  // próxima página lógica disponible

    static constexpr PageID META_PAGE = 0;  // página 0 = metadata

    // Layout de la página de metadata:
    //   [0..3]  → nextPage
    //   [4..7]  → rootPageID  (NULL_PAGE si aún no hay árbol)
    static constexpr int META_OFFSET_NEXT = 0;
    static constexpr int META_OFFSET_ROOT = sizeof(int);

    PageID cachedRoot = NULL_PAGE;  // espejo en memoria del root persistido

    void loadMeta() {
        file.seekg(0, ios::end);
        streamsize sz = file.tellg();
        if (sz < PAGE_SIZE) {
            // Archivo nuevo: inicializar metadata
            nextPage   = 1;        // página 0 reservada para meta
            cachedRoot = NULL_PAGE;
            saveMeta();
        } else {
            Page p{};
            file.seekg(0);
            file.read(p.data, PAGE_SIZE);
            memcpy(&nextPage,   p.data + META_OFFSET_NEXT, sizeof(int));
            memcpy(&cachedRoot, p.data + META_OFFSET_ROOT, sizeof(PageID));
        }
    }

    void saveMeta() {
        Page p{};
        memcpy(p.data + META_OFFSET_NEXT, &nextPage,   sizeof(int));
        memcpy(p.data + META_OFFSET_ROOT, &cachedRoot, sizeof(PageID));
        file.seekp(0);
        file.write(p.data, PAGE_SIZE);
        file.flush();
    }

    // -------- contadores de acceso a disco --------
    // Se resetean con resetCounters() al inicio de cada operación lógica.
    // El visitor/parser puede leerlos tras cada llamada a insert/search/rangeSearch.
    mutable int readCount  = 0;
    mutable int writeCount = 0;

public:
    explicit Disk(const string& filename) {
        file.open(filename, ios::in | ios::out | ios::binary);
        if (!file.is_open()) {
            // Crear archivo si no existe
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);
        }
        loadMeta();
    }

    Page read(PageID id) {
        ++readCount;
        Page p{};
        file.seekg((long long)id * PAGE_SIZE);
        file.read(p.data, PAGE_SIZE);
        return p;
    }

    void write(PageID id, const Page& p) {
        ++writeCount;
        file.seekp((long long)id * PAGE_SIZE);
        file.write(p.data, PAGE_SIZE);
        file.flush();
        saveMeta();  // mantener nextPage persistido
    }

    // Reserva una nueva página inicializada a ceros
    PageID alloc() {
        PageID id = nextPage++;
        Page p{};
        write(id, p);  // write ya llama saveMeta
        return id;
    }

    // Persiste el root — BPlusTree lo llama cada vez que root cambia
    void saveRoot(PageID root) {
        cachedRoot = root;
        saveMeta();
    }

    // Retorna el root persistido (NULL_PAGE si el archivo es nuevo)
    PageID loadRoot() const { return cachedRoot; }

    // Resetea contadores — llamar antes de cada operación lógica
    void resetCounters() { readCount = writeCount = 0; }

    // Retorna accesos totales de la última operación
    int totalReads()    const { return readCount;  }
    int totalWrites()   const { return writeCount; }
    int totalAccesses() const { return readCount + writeCount; }

    // Retorna cuántas páginas hay actualmente
    int pageCount() const { return nextPage; }
};

// ===================== B+ TREE =====================
// Índice B+ sobre disco.
// - TKey: tipo de clave (int, float, string fija, etc.)
//         Debe soportar operator<, operator==, operator<= y ser trivialmente copiable.
// - Claves duplicadas: se almacenan todas (útil para rangos con repetidos).
// - El árbol NO conoce el archivo de datos; solo maneja el índice.
//   La conexión con sequential file u otro storage es responsabilidad del parser/visitor.
//
// Uso típico:
//   BPlusTree<int>   idx(disk);
//   BPlusTree<float> idx(disk);

template<typename TKey>
class BPlusTree {
private:
    using NodeT = Node<TKey>;
    static constexpr int MAX_KEYS = NodeT::MAX_KEYS;

    Disk&  disk;
    PageID root;

    // -------- helpers --------

    inline NodeT* asNode(Page& p) {
        return reinterpret_cast<NodeT*>(p.data);
    }

    inline const NodeT* asNode(const Page& p) const {
        return reinterpret_cast<const NodeT*>(p.data);
    }

    // Crea una página vacía con un nodo inicializado
    pair<PageID, Page> newNode(bool isLeaf, PageID parent) {
        PageID id = disk.alloc();
        Page   p  = disk.read(id);
        NodeT* n  = asNode(p);
        n->isLeaf   = isLeaf;
        n->numKeys  = 0;
        n->parent   = parent;
        n->nextLeaf = NULL_PAGE;
        return {id, p};
    }

    // Baja hasta la hoja donde debería estar la clave k
    PageID findLeaf(const TKey& k) const {
        PageID curr = root;
        while (true) {
            Page        p = disk.read(curr);
            const NodeT* n = asNode(p);
            if (n->isLeaf) return curr;

            int i = 0;
            while (i < n->numKeys && !(k < n->keys[i])) i++;
            curr = n->children[i];
        }
    }

    // -------- insert helpers --------

    void insertIntoLeaf(PageID leafId, const TKey& k, const RID& rid) {
        Page  p = disk.read(leafId);
        NodeT* n = asNode(p);

        int i = n->numKeys - 1;
        while (i >= 0 && n->keys[i] > k) {
            n->keys[i + 1]   = n->keys[i];
            n->values[i + 1] = n->values[i];
            i--;
        }
        n->keys[i + 1]   = k;
        n->values[i + 1] = rid;
        n->numKeys++;

        disk.write(leafId, p);

        if (n->numKeys > MAX_KEYS)
            splitLeaf(leafId);
    }

    // Split de hoja — copy-up de la primera clave del hijo derecho
    void splitLeaf(PageID leafId) {
        Page  lp = disk.read(leafId);
        NodeT* ln = asNode(lp);

        auto [newId, np] = newNode(true, ln->parent);
        NodeT* nn = asNode(np);

        int mid = ln->numKeys / 2;

        nn->numKeys = ln->numKeys - mid;
        for (int i = 0; i < nn->numKeys; i++) {
            nn->keys[i]   = ln->keys[mid + i];
            nn->values[i] = ln->values[mid + i];
        }

        nn->nextLeaf = ln->nextLeaf;
        ln->nextLeaf = newId;
        ln->numKeys  = mid;

        disk.write(leafId, lp);
        disk.write(newId,  np);

        insertInParent(leafId, nn->keys[0], newId);
    }

    // Inserta (left, key, right) en el padre de left
    void insertInParent(PageID left, const TKey& key, PageID right) {
        if (left == root) {
            auto [newRoot, rp] = newNode(false, NULL_PAGE);
            NodeT* r = asNode(rp);

            r->numKeys     = 1;
            r->keys[0]     = key;
            r->children[0] = left;
            r->children[1] = right;

            disk.write(newRoot, rp);
            setParent(left,  newRoot);
            setParent(right, newRoot);

            root = newRoot;
            disk.saveRoot(root);  // persistir nueva raíz
            return;
        }

        Page  lp       = disk.read(left);
        PageID parentId = asNode(lp)->parent;

        Page  pp     = disk.read(parentId);
        NodeT* parent = asNode(pp);

        int i = parent->numKeys - 1;
        while (i >= 0 && parent->keys[i] > key) {
            parent->keys[i + 1]     = parent->keys[i];
            parent->children[i + 2] = parent->children[i + 1];
            i--;
        }
        parent->keys[i + 1]     = key;
        parent->children[i + 2] = right;
        parent->numKeys++;

        disk.write(parentId, pp);
        setParent(right, parentId);

        if (parent->numKeys > MAX_KEYS)
            splitInternal(parentId);
    }

    // Split de nodo interno — push-up de la clave media
    void splitInternal(PageID id) {
        Page  lp = disk.read(id);
        NodeT* ln = asNode(lp);

        auto [newId, np] = newNode(false, ln->parent);
        NodeT* nn = asNode(np);

        int  mid   = ln->numKeys / 2;
        TKey upKey = ln->keys[mid];

        nn->numKeys = ln->numKeys - mid - 1;
        for (int i = 0; i < nn->numKeys; i++) {
            nn->keys[i]     = ln->keys[mid + 1 + i];
            nn->children[i] = ln->children[mid + 1 + i];
        }
        nn->children[nn->numKeys] = ln->children[ln->numKeys];

        ln->numKeys = mid;

        disk.write(id,    lp);
        disk.write(newId, np);

        for (int i = 0; i <= nn->numKeys; i++)
            setParent(nn->children[i], newId);

        insertInParent(id, upKey, newId);
    }

    void setParent(PageID childId, PageID parentId) {
        Page  cp = disk.read(childId);
        NodeT* cn = asNode(cp);
        cn->parent = parentId;
        disk.write(childId, cp);
    }

public:
    // Constructor:
    // - Si el Disk ya tiene un root persistido lo reutiliza automáticamente.
    // - Si es archivo nuevo crea la hoja raíz inicial y la persiste.
    explicit BPlusTree(Disk& d) : disk(d) {
        PageID persisted = disk.loadRoot();
        if (persisted != NULL_PAGE) {
            root = persisted;  // árbol ya existente en disco
            return;
        }
        // Árbol nuevo: crear hoja raíz
        auto [id, p] = newNode(true, NULL_PAGE);
        disk.write(id, p);
        root = id;
        disk.saveRoot(root);
    }

    PageID getRoot() const { return root; }

    // ================= SEARCH (puntual) =================
    // Retorna el primer RID con clave == key, o NULL_RID si no existe.
    RID search(const TKey& key) const {
        PageID curr = root;
        while (true) {
            Page        p = disk.read(curr);
            const NodeT* n = asNode(p);

            if (n->isLeaf) {
                int lo = 0, hi = n->numKeys - 1;
                while (lo <= hi) {
                    int mid = (lo + hi) / 2;
                    if      (n->keys[mid] == key) return n->values[mid];
                    else if (n->keys[mid] <  key) lo = mid + 1;
                    else                          hi = mid - 1;
                }
                return NULL_RID;
            }

            int lo = 0, hi = n->numKeys - 1, idx = n->numKeys;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (key < n->keys[mid]) { idx = mid; hi = mid - 1; }
                else                    lo  = mid + 1;
            }
            curr = n->children[idx];
        }
    }

    // ================= SEARCH (duplicados) =================
    // Retorna todos los RIDs con clave == key.
    vector<RID> searchAll(const TKey& key) const {
        vector<RID> res;

        PageID curr = root;
        while (true) {
            Page        p = disk.read(curr);
            const NodeT* n = asNode(p);
            if (n->isLeaf) break;
            int i = 0;
            while (i < n->numKeys && !(key < n->keys[i])) i++;
            curr = n->children[i];
        }

        while (curr != NULL_PAGE) {
            Page        p = disk.read(curr);
            const NodeT* n = asNode(p);

            bool found = false;
            for (int i = 0; i < n->numKeys; i++) {
                if (n->keys[i] == key) {
                    res.push_back(n->values[i]);
                    found = true;
                } else if (key < n->keys[i]) {
                    return res;
                }
            }

            curr = (found || (n->numKeys > 0 && !(n->keys[n->numKeys-1] < key)))
                   ? n->nextLeaf : NULL_PAGE;
        }

        return res;
    }

    // ================= RANGE SEARCH =================
    // Retorna todos los RIDs cuya clave esté en [a, b].
    vector<RID> rangeSearch(const TKey& a, const TKey& b) const {
        if (b < a) return {};

        vector<RID> res;

        PageID curr = root;
        while (true) {
            Page        p = disk.read(curr);
            const NodeT* n = asNode(p);
            if (n->isLeaf) break;
            int i = 0;
            while (i < n->numKeys && !(a < n->keys[i])) i++;
            curr = n->children[i];
        }

        while (curr != NULL_PAGE) {
            Page        p = disk.read(curr);
            const NodeT* n = asNode(p);

            bool pastEnd = false;
            for (int i = 0; i < n->numKeys; i++) {
                if (b < n->keys[i]) { pastEnd = true; break; }
                if (!(n->keys[i] < a))
                    res.push_back(n->values[i]);
            }

            if (pastEnd) break;
            curr = n->nextLeaf;
        }

        return res;
    }

    // ================= INSERT =================
    // Permite duplicados. Si no los deseas, llama search() antes.
    void insert(const TKey& key, const RID& rid) {
        PageID leaf = findLeaf(key);
        insertIntoLeaf(leaf, key, rid);
    }

    // ================= DEBUG =================
    void printLeaves() const {
        PageID curr = root;
        while (true) {
            Page        p = disk.read(curr);
            const NodeT* n = asNode(p);
            if (n->isLeaf) break;
            curr = n->children[0];
        }

        cout << "[Leaves] ";
        while (curr != NULL_PAGE) {
            Page        p = disk.read(curr);
            const NodeT* n = asNode(p);
            cout << "| ";
            for (int i = 0; i < n->numKeys; i++)
                cout << n->keys[i] << " ";
            curr = n->nextLeaf;
        }
        cout << "|\n";
    }
};