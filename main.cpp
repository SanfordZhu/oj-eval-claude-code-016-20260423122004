#include <bits/stdc++.h>
using namespace std;

const int PAGE_SIZE = 4096;
const int MAX_KEY_LEN = 64;
const int HEADER_SIZE = 16;
const int KEY_SPACE = PAGE_SIZE - HEADER_SIZE;
const int ENTRY_SIZE = MAX_KEY_LEN + 4;
const int MAX_KEYS = KEY_SPACE / ENTRY_SIZE;

const char* DB_FILE = "bptree.db";

struct Node {
    bool is_leaf;
    int key_count;
    int next_page;
    char data[PAGE_SIZE - HEADER_SIZE];

    Node() : is_leaf(false), key_count(0), next_page(-1) {
        memset(data, 0, sizeof(data));
    }

    void set_key(int idx, const char* key) {
        memcpy(data + idx * ENTRY_SIZE, key, MAX_KEY_LEN);
    }

    const char* get_key(int idx) const {
        return data + idx * ENTRY_SIZE;
    }

    void set_value(int idx, int value) {
        memcpy(data + idx * ENTRY_SIZE + MAX_KEY_LEN, &value, 4);
    }

    int get_value(int idx) const {
        int value;
        memcpy(&value, data + idx * ENTRY_SIZE + MAX_KEY_LEN, 4);
        return value;
    }

    void set_child(int idx, int child) {
        memcpy(data + idx * 4, &child, 4);
    }

    int get_child(int idx) const {
        int child;
        memcpy(&child, data + idx * 4, 4);
        return child;
    }
};

class BPTree {
private:
    fstream db;
    int root_page;
    int free_page_head;
    unordered_map<int, Node*> cache;
    list<int> lru_list;
    static const int CACHE_SIZE = 500;

    void lru_update(int page) {
        auto it = std::find(lru_list.begin(), lru_list.end(), page);
        if (it != lru_list.end()) {
            lru_list.erase(it);
        }
        lru_list.push_front(page);
    }

    void lru_evict() {
        if (lru_list.size() > CACHE_SIZE) {
            int page = lru_list.back();
            lru_list.pop_back();
            if (page != root_page && cache.count(page)) {
                write_page(page, cache[page]);
                delete cache[page];
                cache.erase(page);
            }
        }
    }

    int alloc_page() {
        if (free_page_head != -1) {
            int page = free_page_head;
            Node* n = load_page(page);
            free_page_head = n->next_page;
            write_page(page, n);
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
    }

    Node* load_page(int page) {
        if (cache.count(page)) {
            lru_update(page);
            return cache[page];
        }
        Node* n = new Node();
        db.seekg(page * PAGE_SIZE);
        db.read((char*)n, PAGE_SIZE);
        lru_evict();
        lru_update(page);
        cache[page] = n;
        return n;
    }

    void write_page(int page, Node* n) {
        db.seekp(page * PAGE_SIZE);
        db.write((char*)n, PAGE_SIZE);
        db.flush();
    }

    int compare_key(const char* a, const char* b) {
        return strcmp(a, b);
    }

    void insert_to_leaf(Node* leaf, const char* key, int value) {
        int i = leaf->key_count - 1;
        while (i >= 0 && compare_key(key, leaf->get_key(i)) < 0) {
            leaf->set_key(i + 1, leaf->get_key(i));
            leaf->set_value(i + 1, leaf->get_value(i));
            i--;
        }
        leaf->set_key(i + 1, key);
        leaf->set_value(i + 1, value);
        leaf->key_count++;
    }

    void split_leaf(int page, Node* leaf, char* split_key, int& new_page) {
        Node* new_leaf = new Node();
        new_leaf->is_leaf = true;
        int mid = leaf->key_count / 2;
        new_leaf->key_count = leaf->key_count - mid;
        leaf->key_count = mid;

        for (int i = 0; i < new_leaf->key_count; i++) {
            new_leaf->set_key(i, leaf->get_key(mid + i));
            new_leaf->set_value(i, leaf->get_value(mid + i));
        }

        new_leaf->next_page = leaf->next_page;
        leaf->next_page = alloc_page();
        new_page = leaf->next_page;
        write_page(new_page, new_leaf);
        delete new_leaf;
        strcpy(split_key, leaf->get_key(0));
    }

    void insert_to_internal(Node* node, const char* key, int right_child) {
        int i = node->key_count - 1;
        while (i >= 0 && compare_key(key, node->get_key(i)) < 0) {
            node->set_key(i + 1, node->get_key(i));
            node->set_child(i + 2, node->get_child(i + 1));
            i--;
        }
        node->set_key(i + 1, key);
        node->set_child(i + 2, right_child);
        node->key_count++;
    }

    void split_internal(int page, Node* node, char* split_key, int& new_page) {
        Node* new_node = new Node();
        new_node->is_leaf = false;
        int mid = node->key_count / 2;
        new_node->key_count = node->key_count - mid - 1;
        node->key_count = mid;

        strcpy(split_key, node->get_key(mid));

        for (int i = 0; i < new_node->key_count; i++) {
            new_node->set_key(i, node->get_key(mid + 1 + i));
        }
        for (int i = 0; i <= new_node->key_count; i++) {
            new_node->set_child(i, node->get_child(mid + 1 + i));
        }

        new_page = alloc_page();
        write_page(new_page, new_node);
        delete new_node;
    }

    bool insert(int page, const char* key, int value, int& new_page, char* split_key, bool& split) {
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
            return true;
        }

        int child = 0;
        while (child < node->key_count && compare_key(key, node->get_key(child)) >= 0) {
            child++;
        }

        int child_page = node->get_child(child);
        int new_child_page;
        char child_split_key[MAX_KEY_LEN + 1];
        bool child_split;

        if (!insert(child_page, key, value, new_child_page, child_split_key, child_split)) {
            return false;
        }

        if (child_split) {
            if (node->key_count < MAX_KEYS) {
                insert_to_internal(node, child_split_key, new_child_page);
                write_page(page, node);
            } else {
                split_internal(page, node, split_key, new_page);
                if (compare_key(child_split_key, split_key) < 0) {
                    insert_to_internal(node, child_split_key, new_child_page);
                    write_page(page, node);
                } else {
                    Node* new_node = load_page(new_page);
                    insert_to_internal(new_node, child_split_key, new_child_page);
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
            if (strcmp(leaf->get_key(i), key) == 0 && leaf->get_value(i) == value) {
                idx = i;
                break;
            }
        }
        if (idx == -1) return false;

        for (int i = idx; i < leaf->key_count - 1; i++) {
            leaf->set_key(i, leaf->get_key(i + 1));
            leaf->set_value(i, leaf->get_value(i + 1));
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
        while (child < node->key_count && compare_key(key, node->get_key(child)) >= 0) {
            child++;
        }

        return remove(node->get_child(child), key, value);
    }

    void find_values(int page, const char* key, vector<int>& result) {
        Node* node = load_page(page);

        if (node->is_leaf) {
            for (int i = 0; i < node->key_count; i++) {
                if (strcmp(node->get_key(i), key) == 0) {
                    result.push_back(node->get_value(i));
                }
            }
            return;
        }

        int child = 0;
        while (child < node->key_count && compare_key(key, node->get_key(child)) >= 0) {
            child++;
        }
        find_values(node->get_child(child), key, result);
    }

public:
    BPTree() : root_page(-1), free_page_head(-1) {
        bool exists = ifstream(DB_FILE).good();
        db.open(DB_FILE, ios::in | ios::out | ios::binary);

        if (!exists || db.peek() == ifstream::traits_type::eof()) {
            db.close();
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
        bool split;

        if (insert(root_page, key, value, new_page, split_key, split)) {
            if (split) {
                int new_root = alloc_page();
                Node* root = new Node();
                root->is_leaf = false;
                root->key_count = 1;
                root->set_key(0, split_key);
                root->set_child(0, root_page);
                root->set_child(1, new_page);
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
