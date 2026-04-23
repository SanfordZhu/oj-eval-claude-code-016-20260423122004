#include <bits/stdc++.h>
#include <fcntl.h>
#include <unistd.h>
using namespace std;

const char* DB_FILE = "bptree.db";
const char* IDX_FILE = "bptree.idx";

struct Entry {
    char key[64];
    int value;
    int next;
};

class BPTree {
private:
    int data_fd;
    int idx_fd;
    unordered_map<string, int> head_index;
    int num_entries;

    void flush_index() {
        char buf[4];
        memcpy(buf, &num_entries, 4);
        pwrite(idx_fd, buf, 4, 0);
        fsync(idx_fd);
    }

    void load_index() {
        char buf[4];
        pread(idx_fd, buf, 4, 0);
        memcpy(&num_entries, buf, 4);

        head_index.clear();

        if (num_entries > 0) {
            char entry_buf[72];
            for (int i = 0; i < num_entries; i++) {
                pread(data_fd, entry_buf, 72, i * 72);
                string key(entry_buf, 64);
                key = key.c_str();
                if (!head_index.count(key)) {
                    head_index[key] = i;
                }
            }
        } else {
            num_entries = 0;
        }
    }

    void write_entry(int pos, const string& key, int value, int next) {
        char buf[72];
        memset(buf, 0, 72);
        memcpy(buf, key.c_str(), key.size());
        memcpy(buf + 64, &value, 4);
        memcpy(buf + 68, &next, 4);
        pwrite(data_fd, buf, 72, pos * 72);
    }

    void read_entry(int pos, string& key, int& value, int& next) {
        char buf[72];
        pread(data_fd, buf, 72, pos * 72);
        key = string(buf, 64);
        key = key.c_str();
        memcpy(&value, buf + 64, 4);
        memcpy(&next, buf + 68, 4);
    }

    void update_next(int pos, int new_next) {
        char buf[72];
        pread(data_fd, buf, 72, pos * 72);
        memcpy(buf + 68, &new_next, 4);
        pwrite(data_fd, buf, 72, pos * 72);
    }

public:
    BPTree() : data_fd(-1), idx_fd(-1), num_entries(0) {
        bool idx_exists = access(IDX_FILE, F_OK) == 0;

        data_fd = open(DB_FILE, O_RDWR | O_CREAT, 0644);
        idx_fd = open(IDX_FILE, O_RDWR | O_CREAT, 0644);

        if (idx_exists) {
            load_index();
        } else {
            num_entries = 0;
            flush_index();
        }
    }

    ~BPTree() {
        flush_index();
        if (data_fd >= 0) {
            fsync(data_fd);
            close(data_fd);
        }
        if (idx_fd >= 0) {
            close(idx_fd);
        }
    }

    void insert(const char* key, int value) {
        string k(key);

        int pos = num_entries;
        write_entry(pos, k, value, -1);

        if (head_index.count(k)) {
            int head_pos = head_index[k];
            update_next(head_pos, pos);
        } else {
            head_index[k] = pos;
        }

        num_entries++;
        flush_index();
    }

    void remove(const char* key, int value) {
        string k(key);

        if (!head_index.count(k)) return;

        int pos = head_index[k];
        string cur_key;
        int cur_val, cur_next;
        read_entry(pos, cur_key, cur_val, cur_next);

        if (cur_val == value) {
            head_index.erase(k);
            num_entries--;
            flush_index();
            return;
        }

        int prev_pos = pos;
        while (cur_next != -1) {
            int next_pos = cur_next;
            int dummy_val;
            read_entry(next_pos, cur_key, dummy_val, cur_next);
            if (dummy_val == value) {
                string dummy_key;
                int dummy_next;
                read_entry(prev_pos, dummy_key, dummy_val, dummy_next);
                write_entry(prev_pos, k, dummy_val, cur_next);
                head_index.erase(k);
                num_entries--;
                flush_index();
                return;
            }
            prev_pos = next_pos;
        }
    }

    vector<int> find(const char* key) {
        vector<int> result;
        string k(key);

        if (!head_index.count(k)) return result;

        int pos = head_index[k];
        string cur_key;
        int cur_val, cur_next;
        read_entry(pos, cur_key, cur_val, cur_next);

        if (cur_val != -1) result.push_back(cur_val);

        while (cur_next != -1) {
            int next_pos = cur_next;
            read_entry(next_pos, cur_key, cur_val, cur_next);
            if (cur_val != -1) result.push_back(cur_val);
        }

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
