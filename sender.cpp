#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Button.H>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

#include <vector>
#include <queue>
#include <string>
#include <chrono>
#include <cstring>
#include <iostream>
#include <cstdio>

const uint32_t MAX_PACKET_SIZE = 65536;
const uint32_t MAX_OPS_PER_PACKET = 40;
const uint32_t MAX_PACKET_BYTES = 8192;
const uint32_t BASE_BATCH_TIMEOUT_MS = 100;
const uint32_t MIN_BATCH_TIMEOUT_MS = 50;
const uint32_t MAX_BATCH_TIMEOUT_MS = 300;
const uint32_t HEARTBEAT_INTERVAL_MS = 1000;
const uint32_t CONNECTION_TIMEOUT_SEC = 10;

uint32_t htonl_safe(uint32_t x) { return htonl(x); }
uint64_t htonll(uint64_t x) { return ((uint64_t)htonl(x & 0xFFFFFFFF) << 32) | htonl(x >> 32); }
uint32_t ntohl_safe(uint32_t x) { return ntohl(x); }
uint64_t ntohll(uint64_t x) { return ((uint64_t)ntohl(x & 0xFFFFFFFF) << 32) | ntohl(x >> 32); }

struct Operation {
    char op_type;
    uint64_t op_seq;
    uint32_t line;
    uint32_t col;
    std::string text;
};

struct Packet {
    uint8_t flags;
    uint64_t packet_no;
    uint32_t client_id_len;
    std::string client_id;
    std::vector<Operation> ops;
};

class SenderApp {
private:
    Fl_Window* window = nullptr;
    Fl_Text_Editor* editor = nullptr;
    Fl_Text_Buffer* buffer = nullptr;
    Fl_Box* status_box = nullptr;
    Fl_Browser* log_browser = nullptr;
    
    Fl_Box* ip_label = nullptr;
    Fl_Input* ip_input = nullptr;
    Fl_Box* port_label = nullptr;
    Fl_Int_Input* port_input = nullptr;
    Fl_Button* connect_button = nullptr;
    Fl_Button* disconnect_button = nullptr;
    Fl_Button* test_connection_button = nullptr;
    
    Fl_Box* connection_info_box = nullptr;
    Fl_Box* instructions_box = nullptr;

    int sock_fd = -1;
    bool connected = false;
    bool connect_in_progress = false;
    std::string client_id;
    std::string target_ip = "127.0.0.1";
    int target_port = 8888;
    bool ignore_text_changes = false;

    uint64_t next_op_seq = 1;
    uint64_t next_packet_no = 1;
    std::vector<Operation> pending_ops;
    std::vector<std::vector<uint8_t>> send_queue;

    std::chrono::steady_clock::time_point last_keystroke;
    std::chrono::steady_clock::time_point last_heartbeat_sent;
    std::chrono::steady_clock::time_point connect_start_time;
    uint32_t current_batch_timeout = BASE_BATCH_TIMEOUT_MS;

public:
    SenderApp() {
        client_id = "sender_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 100000);
        fprintf(stderr, "SenderApp ctor: client_id=%s\n", client_id.c_str());
        setup_gui();
        Fl::add_timeout(0.03, &SenderApp::poll_cb, this);
    }

    ~SenderApp() {
        if (sock_fd >= 0) close(sock_fd);
    }

    bool is_valid_ip(const std::string& ip) {
        struct sockaddr_in sa;
        int result = inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr));
        return result == 1;
    }

    bool is_valid_hostname(const std::string& hostname) {
        if (hostname.empty() || hostname.length() > 253) return false;
        
        // Check for valid characters
        for (char c : hostname) {
            if (!(isalnum(c) || c == '.' || c == '-')) return false;
        }
        
        // Basic domain format check
        if (hostname[0] == '.' || hostname[0] == '-' || 
            hostname[hostname.length()-1] == '.' || hostname[hostname.length()-1] == '-') {
            return false;
        }
        
        return true;
    }

    std::string resolve_hostname(const std::string& hostname) {
        struct hostent* he = gethostbyname(hostname.c_str());
        if (!he) return "";
        
        struct in_addr addr;
        memcpy(&addr, he->h_addr_list[0], he->h_length);
        return std::string(inet_ntoa(addr));
    }

    void setup_gui() {
        fprintf(stderr, "setup_gui: creating FLTK window\n");
        window = new Fl_Window(800, 700, "P2P Notepad - Sender (Internet Ready)");

        connection_info_box = new Fl_Box(10, 10, 780, 30, "Enter receiver's IP address or hostname below");
        connection_info_box->box(FL_UP_BOX);
        connection_info_box->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        connection_info_box->labelfont(FL_BOLD);
        connection_info_box->labelcolor(FL_DARK_BLUE);

        ip_label = new Fl_Box(10, 48, 120, 28, "Receiver IP/Host:");
        ip_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        ip_label->labelfont(FL_BOLD);

        ip_input = new Fl_Input(140, 48, 250, 28);
        ip_input->value("127.0.0.1");
        ip_input->callback(ip_changed_cb, this);

        port_label = new Fl_Box(400, 48, 40, 28, "Port:");
        port_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        port_label->labelfont(FL_BOLD);

        port_input = new Fl_Int_Input(450, 48, 80, 28);
        port_input->value("8888");
        port_input->callback(port_changed_cb, this);

        test_connection_button = new Fl_Button(540, 48, 100, 28, "Test Connect");
        test_connection_button->callback(test_connection_cb, this);

        connect_button = new Fl_Button(650, 48, 80, 28, "Connect");
        connect_button->callback(connect_cb, this);

        disconnect_button = new Fl_Button(740, 48, 50, 28, "Disc");
        disconnect_button->callback(disconnect_cb, this);
        disconnect_button->deactivate();

        instructions_box = new Fl_Box(10, 84, 780, 40);
        instructions_box->copy_label("For LOCAL network: Use receiver's local IP (192.168.x.x). For INTERNET: Use receiver's public IP and ensure port is forwarded.");
        instructions_box->box(FL_DOWN_BOX);
        instructions_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        instructions_box->labelfont(FL_ITALIC);

        status_box = new Fl_Box(10, 132, 780, 24, "Status: Ready to connect");
        status_box->box(FL_DOWN_BOX);
        status_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        buffer = new Fl_Text_Buffer();
        editor = new Fl_Text_Editor(10, 164, 780, 350);
        editor->buffer(buffer);
        editor->wrap_mode(Fl_Text_Editor::WRAP_AT_BOUNDS, 0);

        Fl_Box* log_label = new Fl_Box(10, 524, 780, 20, "Activity Log:");
        log_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        log_label->labelfont(FL_BOLD);

        log_browser = new Fl_Browser(10, 549, 780, 140);
        log_browser->box(FL_DOWN_BOX);

        buffer->add_modify_callback([](int pos, int nInserted, int nDeleted, int, const char* deletedText, void* data) {
            auto* app = static_cast<SenderApp*>(data);
            app->on_text_change(pos, nInserted, nDeleted, deletedText);
        }, this);

        window->end();
        window->show();

        log_message("Application started. Configure connection settings above.");
        log_message("For internet connectivity, ensure receiver has port forwarding configured.");
        fprintf(stderr, "setup_gui: done\n");
    }

    static void ip_changed_cb(Fl_Widget* w, void* data) {
        SenderApp* app = static_cast<SenderApp*>(data);
        Fl_Input* input = static_cast<Fl_Input*>(w);
        if (app && input) {
            app->target_ip = input->value();
            
            std::string validation_msg;
            if (app->is_valid_ip(app->target_ip)) {
                validation_msg = "Valid IP address: " + app->target_ip;
            } else if (app->is_valid_hostname(app->target_ip)) {
                validation_msg = "Hostname entered: " + app->target_ip + " (will resolve on connect)";
            } else {
                validation_msg = "Invalid IP/hostname format: " + app->target_ip;
            }
            
            app->log_message(validation_msg);
        }
    }

    static void port_changed_cb(Fl_Widget* w, void* data) {
        SenderApp* app = static_cast<SenderApp*>(data);
        Fl_Int_Input* input = static_cast<Fl_Int_Input*>(w);
        if (app && input) {
            const char* port_str = input->value();
            if (port_str && strlen(port_str) > 0) {
                int port = atoi(port_str);
                if (port > 0 && port <= 65535) {
                    app->target_port = port;
                    app->log_message("Target port set to: " + std::to_string(app->target_port));
                } else {
                    app->log_message("Invalid port number (must be 1-65535): " + std::string(port_str));
                }
            }
        }
    }

    static void test_connection_cb(Fl_Widget*, void* data) {
        SenderApp* app = static_cast<SenderApp*>(data);
        if (app && !app->connected && !app->connect_in_progress) {
            app->test_connection();
        }
    }

    static void connect_cb(Fl_Widget*, void* data) {
        SenderApp* app = static_cast<SenderApp*>(data);
        if (app) {
            app->initiate_connection();
        }
    }

    static void disconnect_cb(Fl_Widget*, void* data) {
        SenderApp* app = static_cast<SenderApp*>(data);
        if (app) {
            app->disconnect();
        }
    }

    void test_connection() {
        log_message("Testing connection to " + target_ip + ":" + std::to_string(target_port));
        
        std::string resolved_ip = target_ip;
        if (!is_valid_ip(target_ip)) {
            if (is_valid_hostname(target_ip)) {
                resolved_ip = resolve_hostname(target_ip);
                if (resolved_ip.empty()) {
                    log_message("Test failed: Could not resolve hostname " + target_ip);
                    return;
                }
                log_message("Hostname resolved: " + target_ip + " -> " + resolved_ip);
            } else {
                log_message("Test failed: Invalid IP address or hostname format");
                return;
            }
        }
        
        int test_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (test_sock < 0) {
            log_message("Test failed: Could not create socket");
            return;
        }
        
        // Set timeout for test connection
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(test_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(test_sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(target_port);
        inet_pton(AF_INET, resolved_ip.c_str(), &addr.sin_addr);
        
        if (connect(test_sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            log_message("Test successful: Receiver is reachable at " + resolved_ip + ":" + std::to_string(target_port));
        } else {
            log_message("Test failed: Cannot reach receiver at " + resolved_ip + ":" + std::to_string(target_port));
            log_message("Check: 1) Receiver is running 2) IP/port correct 3) Firewall/port forwarding");
        }
        
        close(test_sock);
    }

    void initiate_connection() {
        if (connected || connect_in_progress) return;
        
        target_ip = ip_input->value();
        const char* port_str = port_input->value();
        if (port_str && strlen(port_str) > 0) {
            int port = atoi(port_str);
            if (port > 0 && port <= 65535) {
                target_port = port;
            } else {
                log_message("Invalid port number - using default 8888");
                target_port = 8888;
            }
        }

        log_message("Attempting to connect to " + target_ip + ":" + std::to_string(target_port));
        connect_button->deactivate();
        test_connection_button->deactivate();
    }

    void disconnect() {
        if (connected && sock_fd >= 0) {
            close(sock_fd);
            sock_fd = -1;
        }
        connected = false;
        connect_in_progress = false;
        
        status_box->label("Status: Disconnected");
        status_box->redraw();
        connect_button->activate();
        test_connection_button->activate();
        disconnect_button->deactivate();
        
        pending_ops.clear();
        send_queue.clear();
        
        log_message("Disconnected from receiver");
    }

    static void poll_cb(void* userdata) {
        SenderApp* a = static_cast<SenderApp*>(userdata);
        if (a) {
            a->poll();
            Fl::repeat_timeout(0.03, &SenderApp::poll_cb, userdata);
        }
    }

    void poll() {
        if (!connected && !connect_in_progress && connect_button->active() == 0) {
            start_connect_nonblocking();
        } else if (!connected && connect_in_progress) {
            check_connect_completion();
        }

        auto now = std::chrono::steady_clock::now();
        if (!pending_ops.empty()) {
            auto idle_ms = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - last_keystroke).count();
            if (idle_ms >= current_batch_timeout) {
                create_and_queue_packet_from_pending_ops();
                if (idle_ms > 2000) current_batch_timeout = MAX_BATCH_TIMEOUT_MS;
                else current_batch_timeout = MIN_BATCH_TIMEOUT_MS;
            }
        }

        if (connected && !send_queue.empty()) {
            send_queued_packets();
        }

        handle_sync_requests();

        auto hb_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_sent).count();
        if (connected && hb_elapsed >= HEARTBEAT_INTERVAL_MS) {
            send_heartbeat();
            last_heartbeat_sent = now;
        }
    }

    void handle_sync_requests() {
        if (!connected || sock_fd < 0) return;
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock_fd, &read_fds);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        int r = select(sock_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (r > 0 && FD_ISSET(sock_fd, &read_fds)) {
            uint32_t packet_len_net;
            ssize_t rec = recv(sock_fd, &packet_len_net, 4, MSG_DONTWAIT);
            if (rec == 4) {
                uint32_t packet_len = ntohl_safe(packet_len_net);
                if (packet_len == 1) {
                    uint8_t sync_flag;
                    rec = recv(sock_fd, &sync_flag, 1, 0);
                    if (rec == 1 && sync_flag == 0x80) {
                        log_message("Received sync request - sending current document");
                        send_full_document();
                    }
                }
            }
        }
    }

    void send_full_document() {
        if (!connected) return;
        
        char* text = buffer->text();
        std::string content = text ? std::string(text) : std::string();
        if (text) free(text);
        
        if (content.empty()) return;
        
        Operation reset_op;
        reset_op.op_type = 'R';
        reset_op.op_seq = next_op_seq++;
        reset_op.line = 0;
        reset_op.col = 0;
        reset_op.text = content;
        
        Packet packet;
        packet.flags = 0;
        packet.packet_no = next_packet_no++;
        packet.client_id_len = static_cast<uint32_t>(client_id.length());
        packet.client_id = client_id;
        packet.ops.push_back(reset_op);
        
        auto serialized = serialize_packet(packet);
        send_queue.push_back(serialized);
        
        log_message("Queued full document sync (packet #" + std::to_string(packet.packet_no) + ")");
    }

    void on_text_change(int pos, int nInserted, int nDeleted, const char* deletedText) {
        if (!connected || ignore_text_changes) {
            return;
        }

        std::string dbg = "on_text_change: pos=" + std::to_string(pos) +
                " inserted=" + std::to_string(nInserted) +
                " deleted=" + std::to_string(nDeleted);
        fprintf(stderr, "%s\n", dbg.c_str());

        last_keystroke = std::chrono::steady_clock::now();
        current_batch_timeout = MIN_BATCH_TIMEOUT_MS;

        char* post_buf = buffer->text();
        std::string post_text = post_buf ? std::string(post_buf) : std::string();
        if (post_buf) free(post_buf);

        std::string pre_text = post_text;
        if (nInserted > 0) {
            if ((size_t)pos + (size_t)nInserted <= pre_text.size()) {
                pre_text.erase(pos, nInserted);
            } else {
                if ((size_t)pos <= pre_text.size()) pre_text.erase(pos);
            }
        } else if (nDeleted > 0 && deletedText) {
            if ((size_t)pos <= pre_text.size()) {
                pre_text.insert(pos, std::string(deletedText, nDeleted));
            } else {
                pre_text += std::string(deletedText, nDeleted);
            }
        }

        int line = 0, col = 0;
        int clamp_pos = pos;
        if (clamp_pos < 0) clamp_pos = 0;
        if ((size_t)clamp_pos > pre_text.size()) clamp_pos = (int)pre_text.size();
        for (int i = 0; i < clamp_pos; ++i) {
            if (pre_text[i] == '\n') { line++; col = 0; }
            else col++;
        }
        fprintf(stderr, "on_text_change: converted (pre-change) pos=%d to line=%d col=%d\n", pos, line, col);

        std::vector<Operation> operations;

        if (nDeleted > 0 && deletedText) {
            Operation delete_op;
            delete_op.op_type = 'D';
            delete_op.op_seq = next_op_seq++;
            delete_op.line = line;
            delete_op.col = col;
            delete_op.text = std::string(deletedText, nDeleted);
            operations.push_back(delete_op);

            fprintf(stderr, "on_text_change: created DELETE op seq=%llu at line=%d col=%d len=%d text='%s'\n",
                    (unsigned long long)delete_op.op_seq, line, col, nDeleted, delete_op.text.c_str());
        }

        if (nInserted > 0) {
            std::string inserted_text;
            if ((size_t)pos + (size_t)nInserted <= post_text.size()) {
                inserted_text = post_text.substr(pos, nInserted);
            } else if ((size_t)pos <= post_text.size()) {
                inserted_text = post_text.substr(pos);
            } else {
                inserted_text = std::string();
            }

            Operation insert_op;
            insert_op.op_type = 'I';
            insert_op.op_seq = next_op_seq++;
            insert_op.line = line;
            insert_op.col = col;
            insert_op.text = inserted_text;
            operations.push_back(insert_op);

            fprintf(stderr, "on_text_change: created INSERT op seq=%llu at line=%d col=%d text='%s' (len=%zu)\n",
                    (unsigned long long)insert_op.op_seq, line, col, insert_op.text.c_str(), insert_op.text.length());
        }

        for (const auto& op : operations) {
            pending_ops.push_back(op);
        }
    }

    void start_connect_nonblocking() {
        fprintf(stderr, "start_connect_nonblocking: attempting to connect to %s:%d\n", target_ip.c_str(), target_port);
        
        std::string resolved_ip = target_ip;
        if (!is_valid_ip(target_ip)) {
            if (is_valid_hostname(target_ip)) {
                resolved_ip = resolve_hostname(target_ip);
                if (resolved_ip.empty()) {
                    log_message("Connection failed: Could not resolve hostname " + target_ip);
                    connect_button->activate();
                    test_connection_button->activate();
                    return;
                }
                log_message("Hostname resolved: " + target_ip + " -> " + resolved_ip);
            } else {
                log_message("Connection failed: Invalid IP address or hostname format");
                connect_button->activate();
                test_connection_button->activate();
                return;
            }
        }
        
        if (sock_fd >= 0) { close(sock_fd); sock_fd = -1; }
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) { 
            fprintf(stderr, "start_connect_nonblocking: socket() failed\n"); 
            connect_button->activate();
            test_connection_button->activate();
            return; 
        }

        int flags = fcntl(sock_fd, F_GETFL, 0);
        fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(target_port);
        inet_pton(AF_INET, resolved_ip.c_str(), &addr.sin_addr);

        connect_start_time = std::chrono::steady_clock::now();
        int r = connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr));
        if (r == 0) {
            connected = true;
            connect_in_progress = false;
            finalize_connected_socket();
            next_packet_no = 1;
            fprintf(stderr, "start_connect_nonblocking: connected immediately\n");
            status_box->label("Status: Connected");
            status_box->redraw();
            disconnect_button->activate();
            log_message("Connected to receiver at " + resolved_ip + ":" + std::to_string(target_port));
        } else {
            if (errno == EINPROGRESS) {
                connect_in_progress = true;
                status_box->label("Status: Connecting...");
                status_box->redraw();
                fprintf(stderr, "start_connect_nonblocking: connect in progress (EINPROGRESS)\n");
            } else {
                fprintf(stderr, "start_connect_nonblocking: connect() failed errno=%d\n", errno);
                close(sock_fd);
                sock_fd = -1;
                connect_in_progress = false;
                connect_button->activate();
                test_connection_button->activate();
                log_message("Connection failed: " + std::string(strerror(errno)));
            }
        }
    }

    void check_connect_completion() {
        if (sock_fd < 0) { 
            connect_in_progress = false; 
            connect_button->activate(); 
            test_connection_button->activate();
            return; 
        }
        
        // Check timeout
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - connect_start_time).count();
        if (elapsed >= CONNECTION_TIMEOUT_SEC) {
            fprintf(stderr, "check_connect_completion: connection timeout\n");
            close(sock_fd);
            sock_fd = -1;
            connect_in_progress = false;
            connect_button->activate();
            test_connection_button->activate();
            status_box->label("Status: Connection timeout");
            status_box->redraw();
            log_message("Connection timeout - check receiver availability and network connectivity");
            return;
        }
        
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock_fd, &wfds);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 0;
        int sel = select(sock_fd + 1, nullptr, &wfds, nullptr, &tv);
        if (sel > 0 && FD_ISSET(sock_fd, &wfds)) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
                err = errno;
            }
            if (err == 0) {
                connected = true;
                connect_in_progress = false;
                finalize_connected_socket();
                next_packet_no = 1;
                fprintf(stderr, "check_connect_completion: connect completed successfully\n");
                status_box->label("Status: Connected");
                status_box->redraw();
                disconnect_button->activate();
                log_message("Connected to receiver at " + target_ip + ":" + std::to_string(target_port));
            } else {
                fprintf(stderr, "check_connect_completion: connect failed err=%d\n", err);
                close(sock_fd);
                sock_fd = -1;
                connect_in_progress = false;
                connect_button->activate();
                test_connection_button->activate();
                status_box->label("Status: Connection failed");
                status_box->redraw();
                log_message("Connection failed: " + std::string(strerror(err)));
                log_message("Troubleshooting: Check receiver is running, IP/port correct, firewall/NAT settings");
            }
        }
    }

    void finalize_connected_socket() {
        int flag = 1;
        setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        int flags = fcntl(sock_fd, F_GETFL, 0);
        flags &= ~O_NONBLOCK;
        fcntl(sock_fd, F_SETFL, flags);

        last_heartbeat_sent = std::chrono::steady_clock::now();
    }

    void create_and_queue_packet_from_pending_ops() {
        if (pending_ops.empty()) return;

        Packet p;
        p.flags = 0;
        p.packet_no = next_packet_no++;
        p.client_id_len = (uint32_t)client_id.length();
        p.client_id = client_id;

        size_t packet_size = 0;
        size_t ops_collected = 0;
        while (!pending_ops.empty() && p.ops.size() < MAX_OPS_PER_PACKET && packet_size < MAX_PACKET_BYTES) {
            Operation op = pending_ops.front();
            size_t op_sz = 1 + 8 + 4 + 4 + 4 + op.text.length();
            if (packet_size + op_sz > MAX_PACKET_BYTES && !p.ops.empty()) break;
            p.ops.push_back(op);
            packet_size += op_sz;
            ops_collected++;
            pending_ops.erase(pending_ops.begin());
        }

        auto serialized = serialize_packet(p);
        send_queue.push_back(serialized);

        fprintf(stderr, "create_and_queue_packet_from_pending_ops: batched packet #%llu ops=%zu bytes=%zu\n",
                (unsigned long long)p.packet_no, p.ops.size(), serialized.size());

        log_message("Batched " + std::to_string(p.ops.size()) + " ops into packet #" + std::to_string(p.packet_no));
    }

    void send_queued_packets() {
        if (!connected || sock_fd < 0) return;
        while (!send_queue.empty()) {
            auto& data = send_queue.front();
            ssize_t sent = send(sock_fd, data.data(), data.size(), MSG_NOSIGNAL);
            if (sent < 0) {
                fprintf(stderr, "send_queued_packets: send failed errno=%d\n", errno);
                connected = false;
                close(sock_fd);
                sock_fd = -1;
                status_box->label("Status: Connection lost");
                status_box->redraw();
                connect_button->activate();
                test_connection_button->activate();
                disconnect_button->deactivate();
                log_message("Connection lost during send");
                break;
            } else if ((size_t)sent < data.size()) {
                std::vector<uint8_t> remainder(data.begin() + sent, data.end());
                data.swap(remainder);
                fprintf(stderr, "send_queued_packets: partial send %zd/%zu\n", sent, remainder.size() + sent);
                break;
            } else {
                fprintf(stderr, "send_queued_packets: sent %zd bytes\n", sent);
                send_queue.erase(send_queue.begin());
            }
        }
    }

    void send_heartbeat() {
        Packet hb;
        hb.flags = 1;
        hb.packet_no = 0;
        hb.client_id_len = (uint32_t)client_id.length();
        hb.client_id = client_id;
        auto s = serialize_packet(hb);
        if (!connected || sock_fd < 0) return;
        ssize_t sent = send(sock_fd, s.data(), s.size(), MSG_NOSIGNAL);
        if (sent <= 0) {
            fprintf(stderr, "send_heartbeat: send failed (sent=%zd errno=%d)\n", sent, errno);
            connected = false;
            close(sock_fd);
            sock_fd = -1;
            status_box->label("Status: Connection lost");
            status_box->redraw();
            connect_button->activate();
            test_connection_button->activate();
            disconnect_button->deactivate();
            log_message("Heartbeat failed - connection lost");
        } else {
            fprintf(stderr, "send_heartbeat: sent %zd bytes\n", sent);
        }
    }

    std::vector<uint8_t> serialize_packet(const Packet& packet) {
        std::vector<uint8_t> payload;
        payload.push_back(packet.flags);
        uint64_t pn_net = htonll(packet.packet_no);
        payload.insert(payload.end(), (uint8_t*)&pn_net, (uint8_t*)&pn_net + 8);
        uint32_t cid_len_net = htonl_safe(packet.client_id_len);
        payload.insert(payload.end(), (uint8_t*)&cid_len_net, (uint8_t*)&cid_len_net + 4);
        payload.insert(payload.end(), packet.client_id.begin(), packet.client_id.end());

        for (const auto& op : packet.ops) {
            payload.push_back(op.op_type);
            uint64_t seq_net = htonll(op.op_seq);
            payload.insert(payload.end(), (uint8_t*)&seq_net, (uint8_t*)&seq_net + 8);
            uint32_t line_net = htonl_safe(op.line);
            payload.insert(payload.end(), (uint8_t*)&line_net, (uint8_t*)&line_net + 4);
            uint32_t col_net = htonl_safe(op.col);
            payload.insert(payload.end(), (uint8_t*)&col_net, (uint8_t*)&col_net + 4);
            uint32_t text_len_net = htonl_safe((uint32_t)op.text.length());
            payload.insert(payload.end(), (uint8_t*)&text_len_net, (uint8_t*)&text_len_net + 4);
            payload.insert(payload.end(), op.text.begin(), op.text.end());
        }

        uint32_t total = (uint32_t)payload.size();
        uint32_t total_net = htonl_safe(total);
        std::vector<uint8_t> out;
        out.resize(4);
        memcpy(out.data(), &total_net, 4);
        out.insert(out.end(), payload.begin(), payload.end());

        fprintf(stderr, "serialize_packet: packet_no=%llu flags=%u client_id_len=%u ops=%zu total=%zu\n",
                (unsigned long long)packet.packet_no, (unsigned)packet.flags, packet.client_id_len, packet.ops.size(), out.size());
        return out;
    }

    void log_message(const std::string& msg) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &tm);
        std::string line = std::string(timestamp) + " - " + msg;
        if (log_browser) {
            log_browser->add(line.c_str());
            log_browser->bottomline(log_browser->size());
            log_browser->redraw();
        }
        fprintf(stderr, "GUI-LOG: %s\n", line.c_str());
    }

    void run() {
        Fl::run();
    }
};

int main() {
    fprintf(stderr, "main: starting sender application\n");
    SenderApp app;
    app.run();
    return 0;
}