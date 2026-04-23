#include <bits/stdc++.h>
#include <fcntl.h>
#include <unistd.h>
using namespace std;

const int PAGE_SIZE = 4096;
const int MAX_KEY_LEN = 64;
const int HEADER_SIZE = 12;
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
    int fd;
    int root_page;
    int free_page_head;
    int cached_page;
    Node* cached_node;

    void flush_cache() {
        if (cached_page >= 0 && cached_node) {
            pwrite(fd, cached_node, PAGE_SIZE, cached_page * PAGE_SIZE);
        }
    }

    void load_page(int page) {
        if (cached_page == page) return;
        flush_cache();
        cached_page = page;
        if (!cached_node) cached_node = new Node();
        pread(fd, cached_node, PAGE_SIZE, page * PAGE_SIZE);
    }

    void write_page(int page, Node* n) {
        if (cached_page == page) {
            memcpy(cached_node, n, PAGE_SIZE);
            pwrite(fd, cached_node, PAGE_SIZE, page * PAGE_SIZE);
        } else {
            pwrite(fd, n, PAGE_SIZE, page * PAGE_SIZE);
        }
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
        Node new_leaf;
        new_leaf.is_leaf = true;
        int mid = leaf->key_count / 2;
        new_leaf.key_count = leaf->key_count - mid;
        leaf->key_count = mid;

        for (int i = 0; i < new_leaf.key_count; i++) {
            new_leaf.set_key(i, leaf->get_key(mid + i));
            new_leaf.set_value(i, leaf->get_value(mid + i));
        }

        new_leaf.next_page = leaf->next_page;
        leaf->next_page = alloc_page();
        new_page = leaf->next_page;
        write_page(new_page, &new_leaf);
        strcpy(split_key, new_leaf.get_key(0));
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
        Node new_node;
        new_node.is_leaf = false;
        int mid = node->key_count / 2;
        new_node.key_count = node->key_count - mid - 1;
        node->key_count = mid;

        strcpy(split_key, node->get_key(mid));

        for (int i = 0; i < new_node.key_count; i++) {
            new_node.set_key(i, node->get_key(mid + 1 + i));
        }
        for (int i = 0; i <= new_node.key_count; i++) {
            new_node.set_child(i, node->get_child(mid + i));
        }

        new_page = alloc_page();
        write_page(new_page, &new_node);
    }

    bool insert(int page, const char* key, int value, int& new_page, char* split_key, bool& split) {
        load_page(page);
        Node* node = cached_node;
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
                load_page(new_page);
                insert_to_leaf(cached_node, key, value);
                write_page(new_page, cached_node);
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

        load_page(page);
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
                    load_page(new_page);
                    insert_to_internal(cached_node, child_split_key, new_child_page);
                    write_page(new_page, cached_node);
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
        load_page(page);
        Node* node = cached_node;

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
        load_page(page);
        Node* node = cached_node;

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
    BPTree() : root_page(-1), free_page_head(-1), cached_page(-1), cached_node(nullptr), fd(-1) {
        bool exists = access(DB_FILE, F_OK) == 0;
        fd = open(DB_FILE, O_RDWR | O_CREAT, 0644);

        if (!exists || lseek(fd, 0, SEEK_END) == 0) {
            root_page = alloc_page();
            Node root;
            root.is_leaf = true;
            write_page(root_page, &root);
        } else {
            char header[8];
            pread(fd, header, 8, 0);
            memcpy(&root_page, header, 4);
            memcpy(&free_page_head, header + 4, 4);
        }
    }

    ~BPTree() {
        flush_cache();
        if (cached_node) {
            delete cached_node;
            cached_node = nullptr;
        }
        if (fd >= 0) {
            char header[8];
            memcpy(header, &root_page, 4);
            memcpy(header + 4, &free_page_head, 4);
            pwrite(fd, header, 8, 0);
            fsync(fd);
            close(fd);
        }
    }

    int alloc_page() {
        if (free_page_head != -1) {
            int page = free_page_head;
            load_page(page);
            free_page_head = cached_node->next_page;
            write_page(page, cached_node);
            return page;
        }
        off_t size = lseek(fd, 0, SEEK_END);
        int page = size / PAGE_SIZE;
        return page;
    }

    void insert(const char* key, int value) {
        int new_page;
        char split_key[MAX_KEY_LEN + 1];
        bool split;

        if (insert(root_page, key, value, new_page, split_key, split)) {
            if (split) {
                int new_root = alloc_page();
                Node root;
                root.is_leaf = false;
                root.key_count = 1;
                root.set_key(0, split_key);
                root.set_child(0, root_page);
                root.set_child(1, new_page);
                write_page(new_root, &root);
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
