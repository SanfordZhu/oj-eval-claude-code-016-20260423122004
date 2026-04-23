#include <bits/stdc++.h>
using namespace std;

const int PAGE_SIZE = 4096;
const int MAX_KEY_LEN = 64;
const int MAX_KEYS = (PAGE_SIZE - 16) / (MAX_KEY_LEN + 8);
const int MAX_CHILDREN = MAX_KEYS + 1;

const char* DB_FILE = "bptree.db";

struct Node {
    bool is_leaf;
    int key_count;
    int next_page;
    int keys[MAX_KEYS];
    int children[MAX_CHILDREN];
    char key_str[MAX_KEYS][MAX_KEY_LEN + 1];

    Node() : is_leaf(false), key_count(0), next_page(-1) {
        memset(keys, 0, sizeof(keys));
        memset(children, -1, sizeof(children));
        memset(key_str, 0, sizeof(key_str));
    }
};

class BPTree {
private:
    fstream db;
    int root_page;
    int free_page_head;
    unordered_map<int, Node*> cache;
    static const int CACHE_SIZE = 1000;

    int alloc_page() {
        if (free_page_head != -1) {
            int page = free_page_head;
            Node* n = load_page(page);
            free_page_head = n->next_page;
            delete n;
            return page;
        }
        db.seekp(0, ios::end);
        int page = db.tellp() / PAGE_SIZE;
        return page;
    }

    void free_page(int page) {
        if (page < 0) return;
        Node* n = load_page(page);
        n->next_page = free_page_head;
        free_page_head = page;
        write_page(page, n);
        delete n;
    }

    Node* load_page(int page) {
        if (cache.count(page)) return cache[page];
        Node* n = new Node();
        db.seekg(page * PAGE_SIZE);
        db.read((char*)n, PAGE_SIZE);
        if (cache.size() >= CACHE_SIZE) {
            for (auto it = cache.begin(); it != cache.end(); ) {
                if (it->first != root_page) {
                    write_page(it->first, it->second);
                    delete it->second;
                    it = cache.erase(it);
                } else ++it;
            }
        }
        cache[page] = n;
        return n;
    }

    void write_page(int page, Node* n) {
        db.seekp(page * PAGE_SIZE);
        db.write((char*)n, PAGE_SIZE);
        db.flush();
    }

    void uncache(int page) {
        if (cache.count(page)) {
            delete cache[page];
            cache.erase(page);
        }
    }

    int compare_key(const char* a, const char* b) {
        return strcmp(a, b);
    }

    void insert_to_leaf(Node* leaf, const char* key, int value) {
        int i = leaf->key_count - 1;
        while (i >= 0 && compare_key(key, leaf->key_str[i]) < 0) {
            strcpy(leaf->key_str[i + 1], leaf->key_str[i]);
            leaf->keys[i + 1] = leaf->keys[i];
            i--;
        }
        strcpy(leaf->key_str[i + 1], key);
        leaf->keys[i + 1] = value;
        leaf->key_count++;
    }

    void split_leaf(int page, Node* leaf, char* split_key, int& new_page) {
        Node* new_leaf = new Node();
        new_leaf->is_leaf = true;
        int mid = leaf->key_count / 2;
        new_leaf->key_count = leaf->key_count - mid;
        leaf->key_count = mid;

        for (int i = 0; i < new_leaf->key_count; i++) {
            strcpy(new_leaf->key_str[i], leaf->key_str[mid + i]);
            new_leaf->keys[i] = leaf->keys[mid + i];
        }

        new_leaf->next_page = leaf->next_page;
        leaf->next_page = alloc_page();
        new_page = leaf->next_page;
        write_page(new_page, new_leaf);
        delete new_leaf;
        strcpy(split_key, leaf->key_str[0]);
    }

    void insert_to_internal(Node* node, const char* key, int value, int right_child) {
        int i = node->key_count - 1;
        while (i >= 0 && compare_key(key, node->key_str[i]) < 0) {
            strcpy(node->key_str[i + 1], node->key_str[i]);
            node->children[i + 2] = node->children[i + 1];
            i--;
        }
        strcpy(node->key_str[i + 1], key);
        node->keys[i + 1] = value;
        node->children[i + 2] = right_child;
        node->key_count++;
    }

    void split_internal(int page, Node* node, char* split_key, int& new_page, int mid_val) {
        Node* new_node = new Node();
        new_node->is_leaf = false;
        int mid = node->key_count / 2;
        new_node->key_count = node->key_count - mid - 1;
        node->key_count = mid;

        strcpy(split_key, node->key_str[mid]);
        int split_val = node->keys[mid];

        for (int i = 0; i < new_node->key_count; i++) {
            strcpy(new_node->key_str[i], node->key_str[mid + 1 + i]);
            new_node->keys[i] = node->keys[mid + 1 + i];
        }
        for (int i = 0; i <= new_node->key_count; i++) {
            new_node->children[i] = node->children[mid + 1 + i];
        }

        new_page = alloc_page();
        write_page(new_page, new_node);
        delete new_node;
    }

    bool insert(int page, const char* key, int value, int& new_page, char* split_key, int& split_val, bool& split) {
        Node* node = load_page(page);
        split = false;

        if (node->is_leaf) {
            if (node->key_count < MAX_KEYS) {
                insert_to_leaf(node, key, value);
                write_page(page, node);
                return true;
            }
            split_leaf(page, node, split_key, new_page);
            if (compare_key(key, split_key) < 0) {
                insert_to_leaf(node, key, value);
                write_page(page, node);
            } else {
                Node* new_leaf = load_page(new_page);
                insert_to_leaf(new_leaf, key, value);
                write_page(new_page, new_leaf);
            }
            split = true;
            split_val = 0;
            return true;
        }

        int child = 0;
        while (child < node->key_count && compare_key(key, node->key_str[child]) >= 0) {
            child++;
        }

        int child_page = node->children[child];
        int new_child_page;
        char child_split_key[MAX_KEY_LEN + 1];
        int child_split_val;
        bool child_split;

        if (!insert(child_page, key, value, new_child_page, child_split_key, child_split_val, child_split)) {
            return false;
        }

        if (child_split) {
            if (node->key_count < MAX_KEYS) {
                insert_to_internal(node, child_split_key, child_split_val, new_child_page);
                write_page(page, node);
            } else {
                split_internal(page, node, split_key, new_page, split_val);
                if (compare_key(child_split_key, split_key) < 0) {
                    insert_to_internal(node, child_split_key, child_split_val, new_child_page);
                    write_page(page, node);
                } else {
                    Node* new_node = load_page(new_page);
                    insert_to_internal(new_node, child_split_key, child_split_val, new_child_page);
                    write_page(new_page, new_node);
                }
                split = true;
            }
        } else {
            write_page(page, node);
        }
        return true;
    }

    bool remove_from_leaf(Node* leaf, const char* key, int value) {
        int idx = -1;
        for (int i = 0; i < leaf->key_count; i++) {
            if (strcmp(leaf->key_str[i], key) == 0 && leaf->keys[i] == value) {
                idx = i;
                break;
            }
        }
        if (idx == -1) return false;

        for (int i = idx; i < leaf->key_count - 1; i++) {
            strcpy(leaf->key_str[i], leaf->key_str[i + 1]);
            leaf->keys[i] = leaf->keys[i + 1];
        }
        leaf->key_count--;
        return true;
    }

    bool remove(int page, const char* key, int value) {
        Node* node = load_page(page);

        if (node->is_leaf) {
            bool removed = remove_from_leaf(node, key, value);
            if (removed) write_page(page, node);
            return removed;
        }

        int child = 0;
        while (child < node->key_count && compare_key(key, node->key_str[child]) >= 0) {
            child++;
        }

        return remove(node->children[child], key, value);
    }

    void find_values(int page, const char* key, vector<int>& result) {
        Node* node = load_page(page);

        if (node->is_leaf) {
            for (int i = 0; i < node->key_count; i++) {
                if (strcmp(node->key_str[i], key) == 0) {
                    result.push_back(node->keys[i]);
                }
            }
            return;
        }

        int child = 0;
        while (child < node->key_count && compare_key(key, node->key_str[child]) >= 0) {
            child++;
        }
        find_values(node->children[child], key, result);
    }

public:
    BPTree() : root_page(-1), free_page_head(-1) {
        bool exists = ifstream(DB_FILE).good();
        db.open(DB_FILE, ios::in | ios::out | ios::binary);

        if (!exists) {
            db.open(DB_FILE, ios::in | ios::out | ios::binary | ios::trunc);
            root_page = alloc_page();
            Node* root = new Node();
            root->is_leaf = true;
            write_page(root_page, root);
            delete root;
        } else {
            db.seekg(0);
            db.read((char*)&root_page, sizeof(int));
            db.read((char*)&free_page_head, sizeof(int));
        }
    }

    ~BPTree() {
        for (auto& p : cache) {
            write_page(p.first, p.second);
            delete p.second;
        }
        db.seekp(0);
        db.write((char*)&root_page, sizeof(int));
        db.write((char*)&free_page_head, sizeof(int));
        db.flush();
        db.close();
    }

    void insert(const char* key, int value) {
        int new_page;
        char split_key[MAX_KEY_LEN + 1];
        int split_val;
        bool split;

        if (insert(root_page, key, value, new_page, split_key, split_val, split)) {
            if (split) {
                int new_root = alloc_page();
                Node* root = new Node();
                root->is_leaf = false;
                root->key_count = 1;
                strcpy(root->key_str[0], split_key);
                root->keys[0] = split_val;
                root->children[0] = root_page;
                root->children[1] = new_page;
                write_page(new_root, root);
                delete root;
                root_page = new_root;
            }
        }
    }

    void remove(const char* key, int value) {
        remove(root_page, key, value);
    }

    vector<int> find(const char* key) {
        vector<int> result;
        find_values(root_page, key, result);
        return result;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    BPTree tree;

    int n;
    cin >> n;

    for (int i = 0; i < n; i++) {
        string cmd;
        cin >> cmd;

        if (cmd == "insert") {
            string key;
            int value;
            cin >> key >> value;
            tree.insert(key.c_str(), value);
        } else if (cmd == "delete") {
            string key;
            int value;
            cin >> key >> value;
            tree.remove(key.c_str(), value);
        } else if (cmd == "find") {
            string key;
            cin >> key;
            vector<int> result = tree.find(key.c_str());
            if (result.empty()) {
                cout << "null\n";
            } else {
                sort(result.begin(), result.end());
                for (size_t j = 0; j < result.size(); j++) {
                    if (j > 0) cout << " ";
                    cout << result[j];
                }
                cout << "\n";
            }
        }
    }

    return 0;
}
