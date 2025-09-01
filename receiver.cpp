#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Browser.H>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>

#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <iostream>
#include <cstdio>

const uint32_t MAX_PACKET_SIZE = 65536;
const uint32_t HEARTBEAT_TIMEOUT_MS = 3000;

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

class DocumentModel {
private:
    std::vector<std::string> lines;
    uint64_t last_applied_seq = 0;
public:
    DocumentModel() { lines.push_back(""); }
    void apply_operation(const Operation& op) {
        if (op.op_seq <= last_applied_seq) return;
        while (lines.size() <= op.line) lines.push_back("");
        if (op.op_type == 'I') {
            std::string& target_line = lines[op.line];
            size_t insert_pos = std::min((size_t)op.col, target_line.length());
            target_line.insert(insert_pos, op.text);
            size_t line_idx = op.line;
            size_t search_start = insert_pos;
            while (true) {
                size_t nl = lines[line_idx].find('\n', search_start);
                if (nl == std::string::npos) break;
                std::string remaining = lines[line_idx].substr(nl + 1);
                lines[line_idx] = lines[line_idx].substr(0, nl);
                ++line_idx;
                lines.insert(lines.begin() + line_idx, remaining);
                search_start = 0;
            }
        } else if (op.op_type == 'D') {
            if (op.line >= lines.size()) {
            } else {
                if (op.text.find('\n') == std::string::npos) {
                    std::string &target_line = lines[op.line];
                    size_t start_pos = std::min((size_t)op.col, target_line.length());
                    size_t delete_len = std::min(op.text.length(), target_line.length() - start_pos);
                    if (start_pos < target_line.length()) target_line.erase(start_pos, delete_len);
                } else {
                    std::vector<std::string> del_lines;
                    size_t s = 0;
                    while (s < op.text.size()) {
                        size_t p = op.text.find('\n', s);
                        if (p == std::string::npos) {
                            del_lines.push_back(op.text.substr(s));
                            break;
                        } else {
                            del_lines.push_back(op.text.substr(s, p - s));
                            s = p + 1;
                            if (s == op.text.size()) del_lines.push_back("");
                        }
                    }
                    std::string &first_line = lines[op.line];
                    size_t start_pos = std::min((size_t)op.col, first_line.length());
                    std::string prefix = first_line.substr(0, start_pos);
                    std::string suffix;
                    size_t lines_to_remove = del_lines.size() - 1;
                    size_t idx_of_final_line_after = op.line + lines_to_remove;
                    if (idx_of_final_line_after < lines.size()) {
                        std::string &final_line_ref = lines[idx_of_final_line_after];
                        size_t remove_from_final = del_lines.back().length();
                        if (remove_from_final <= final_line_ref.length()) {
                            suffix = final_line_ref.substr(remove_from_final);
                        } else suffix = std::string();
                    } else suffix = std::string();
                    size_t erase_start_index = op.line;
                    size_t erase_count = 1 + lines_to_remove;
                    if (erase_start_index + erase_count > lines.size()) erase_count = lines.size() - erase_start_index;
                    lines[erase_start_index] = prefix;
                    if (erase_count > 1) {
                        lines.erase(lines.begin() + erase_start_index + 1, lines.begin() + erase_start_index + erase_count);
                    }
                    lines[erase_start_index] += suffix;
                }
            }
        } else if (op.op_type == 'R') {
            lines.clear();
            lines.push_back("");
            if (!op.text.empty()) {
                lines[0] = op.text;
                size_t line_idx = 0;
                size_t search_start = 0;
                while (true) {
                    size_t nl = lines[line_idx].find('\n', search_start);
                    if (nl == std::string::npos) break;
                    std::string remaining = lines[line_idx].substr(nl + 1);
                    lines[line_idx] = lines[line_idx].substr(0, nl);
                    ++line_idx;
                    lines.insert(lines.begin() + line_idx, remaining);
                    search_start = 0;
                }
            }
        }
        last_applied_seq = op.op_seq;
    }
    std::string get_content() const {
        std::string content;
        for (size_t i = 0; i < lines.size(); ++i) {
            content += lines[i];
            if (i + 1 < lines.size()) content += "\n";
        }
        return content;
    }
    void reset() {
        lines.clear();
        lines.push_back("");
        last_applied_seq = 0;
    }
};

class ReceiverApp {
private:
    Fl_Window* window = nullptr;
    Fl_Text_Display* display = nullptr;
    Fl_Text_Buffer* buffer = nullptr;
    Fl_Box* status_box = nullptr;
    Fl_Browser* log_browser = nullptr;
    Fl_Box* connection_info_box = nullptr;
    Fl_Box* log_label = nullptr;

    int server_fd = -1;
    int client_fd = -1;
    int port = 8888;
    std::string my_ip;

    std::map<uint64_t, Packet> pending_packets;
    uint64_t expected_packet = 1;
    uint64_t last_applied_packet = 0;

    DocumentModel document;
    std::chrono::steady_clock::time_point last_heartbeat;

public:
    ReceiverApp() {
        detect_local_ip();
        setup_gui();
        setup_server();
        Fl::add_timeout(0.03, &ReceiverApp::network_poll_cb, this);
    }
    ~ReceiverApp() {
        if (client_fd >= 0) close(client_fd);
        if (server_fd >= 0) close(server_fd);
    }

    void detect_local_ip() {
        my_ip = "127.0.0.1";
        
        struct ifaddrs *ifaddrs_ptr = nullptr;
        if (getifaddrs(&ifaddrs_ptr) == -1) {
            fprintf(stderr, "getifaddrs failed\n");
            return;
        }

        for (struct ifaddrs *ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            
            struct sockaddr_in* addr_in = (struct sockaddr_in*)ifa->ifa_addr;
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(addr_in->sin_addr), ip_str, INET_ADDRSTRLEN);
            
            if (strncmp(ip_str, "127.", 4) != 0) {
                my_ip = std::string(ip_str);
                break;
            }
        }
        
        freeifaddrs(ifaddrs_ptr);
        fprintf(stderr, "Detected local IP: %s\n", my_ip.c_str());
    }

    void setup_gui() {
        window = new Fl_Window(800, 640, "P2P Notepad - Receiver");
        
        std::string conn_info = "Listening on: " + my_ip + ":" + std::to_string(port) ;
        connection_info_box = new Fl_Box(10, 10, 780, 32);
        connection_info_box->copy_label(conn_info.c_str());
        connection_info_box->box(FL_UP_BOX);
        connection_info_box->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        connection_info_box->labelfont(FL_BOLD);
        connection_info_box->labelcolor(FL_DARK_GREEN);
        
        status_box = new Fl_Box(10, 52, 780, 24, "Status: Waiting for sender to connect...");
        status_box->box(FL_DOWN_BOX);
        status_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        
        buffer = new Fl_Text_Buffer();
        display = new Fl_Text_Display(10, 86, 780, 394);
        display->buffer(buffer);
        display->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
        
        log_label = new Fl_Box(10, 490, 780, 20, "Activity Log:");
        log_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        log_label->labelfont(FL_BOLD);
        
        log_browser = new Fl_Browser(10, 515, 780, 110);
        log_browser->box(FL_DOWN_BOX);
        
        window->end();
        window->show();
        log_message("Receiver started - ready to accept connections");
        log_message("Your IP address: " + my_ip);
        log_message("Listening on port: " + std::to_string(port));
    }

    void setup_server() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            log_message("Failed to create server socket");
            return;
        }
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(server_fd);
            server_fd = -1;
            log_message("Bind failed - port may be in use");
            return;
        }
        if (listen(server_fd, 1) < 0) {
            close(server_fd);
            server_fd = -1;
            log_message("Listen failed");
            return;
        }
        log_message("Server socket ready - accepting connections from any sender");
    }

    static void network_poll_cb(void* data) {
        ReceiverApp* app = static_cast<ReceiverApp*>(data);
        if (!app) return;
        app->network_poll();
        Fl::repeat_timeout(0.03, &ReceiverApp::network_poll_cb, data);
    }

    void network_poll() {
        if (client_fd < 0) accept_connection_nonblocking();
        else {
            handle_client_data_nonblocking();
            check_heartbeat();
        }
    }

    void accept_connection_nonblocking() {
        if (server_fd < 0) return;
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        int r = select(server_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (r > 0 && FD_ISSET(server_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (fd >= 0) {
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                
                client_fd = fd;
                last_heartbeat = std::chrono::steady_clock::now();
                int flag = 1;
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
                
                status_box->label("Status: Connected - receiving data from sender");
                status_box->redraw();
                log_message("Sender connected from " + std::string(client_ip));
                log_message("Connection established - ready to receive data");
                
                send_sync_request();
                expected_packet = 1;
                last_applied_packet = 0;
            }
        }
    }

    void send_sync_request() {
        if (client_fd < 0) return;
        
        std::vector<uint8_t> sync_packet;
        uint32_t packet_len = 1;
        uint32_t packet_len_net = htonl(packet_len);
        sync_packet.resize(4);
        memcpy(sync_packet.data(), &packet_len_net, 4);
        sync_packet.push_back(0x80);
        
        ssize_t sent = send(client_fd, sync_packet.data(), sync_packet.size(), 0);
        if (sent != (ssize_t)sync_packet.size()) {
            log_message("Failed to send sync request");
        } else {
            log_message("Sent sync request to sender");
        }
    }

    ssize_t read_n(int fd, void* buf, size_t n) {
        size_t total = 0;
        uint8_t* p = static_cast<uint8_t*>(buf);
        while (total < n) {
            ssize_t r = recv(fd, p + total, n - total, 0);
            if (r > 0) total += r;
            else if (r == 0) return 0;
            else {
                if (errno == EINTR) continue;
                return -1;
            }
        }
        return (ssize_t)total;
    }

    void handle_client_data_nonblocking() {
        if (client_fd < 0) return;
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        int r = select(client_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (r <= 0) return;
        uint32_t packet_len_net;
        ssize_t rec = read_n(client_fd, &packet_len_net, 4);
        if (rec <= 0) { disconnect_client(); return; }
        uint32_t packet_len = ntohl_safe(packet_len_net);
        if (packet_len > MAX_PACKET_SIZE) { disconnect_client(); return; }
        std::vector<uint8_t> packet_data(packet_len);
        rec = read_n(client_fd, packet_data.data(), packet_len);
        if (rec != (ssize_t)packet_len) { disconnect_client(); return; }
        Packet packet = parse_packet(packet_data);
        if (packet.flags & 1) last_heartbeat = std::chrono::steady_clock::now();
        else process_packet(packet);
    }

    void disconnect_client() {
        if (client_fd >= 0) close(client_fd);
        client_fd = -1;
        status_box->label("Status: Sender disconnected - waiting for reconnection...");
        status_box->redraw();
        log_message("Sender disconnected");
        pending_packets.clear();
        document.reset();
        buffer->text(document.get_content().c_str());
        display->redraw();
    }

    void check_heartbeat() {
        if (client_fd < 0) return;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat).count();
        if (elapsed > HEARTBEAT_TIMEOUT_MS) {
            log_message("Heartbeat timeout - connection lost");
            disconnect_client();
        }
    }

    Packet parse_packet(const std::vector<uint8_t>& data) {
        Packet packet;
        size_t pos = 0;
        if (pos >= data.size()) return packet;
        packet.flags = data[pos++];
        if (pos + 8 > data.size()) return packet;
        memcpy(&packet.packet_no, data.data() + pos, 8);
        packet.packet_no = ntohll(packet.packet_no);
        pos += 8;
        if (pos + 4 > data.size()) return packet;
        memcpy(&packet.client_id_len, data.data() + pos, 4);
        packet.client_id_len = ntohl_safe(packet.client_id_len);
        pos += 4;
        if (pos + packet.client_id_len > data.size()) return packet;
        packet.client_id = std::string((char*)data.data() + pos, packet.client_id_len);
        pos += packet.client_id_len;
        while (pos < data.size()) {
            Operation op;
            op.op_type = data[pos++];
            if (pos + 8 > data.size()) break;
            memcpy(&op.op_seq, data.data() + pos, 8);
            op.op_seq = ntohll(op.op_seq); pos += 8;
            if (pos + 4 > data.size()) break;
            memcpy(&op.line, data.data() + pos, 4); op.line = ntohl_safe(op.line); pos += 4;
            if (pos + 4 > data.size()) break;
            memcpy(&op.col, data.data() + pos, 4); op.col = ntohl_safe(op.col); pos += 4;
            uint32_t text_len;
            if (pos + 4 > data.size()) break;
            memcpy(&text_len, data.data() + pos, 4); text_len = ntohl_safe(text_len); pos += 4;
            if (pos + text_len > data.size()) break;
            op.text = std::string((char*)data.data() + pos, text_len); pos += text_len;
            packet.ops.push_back(op);
        }
        return packet;
    }

    void process_packet(const Packet& packet) {
        if (packet.packet_no == 0) return;
        pending_packets[packet.packet_no] = packet;
        apply_pending_packets();
        last_heartbeat = std::chrono::steady_clock::now();
    }

    void apply_pending_packets() {
        bool applied_any = false;
        while (true) {
            auto it = pending_packets.find(expected_packet);
            if (it == pending_packets.end()) break;
            Packet pkt = it->second;
            for (size_t i = 0; i < pkt.ops.size(); ++i) {
                const Operation& op = pkt.ops[i];
                if (op.op_type != 'I' && op.op_type != 'D' && op.op_type != 'R') continue;
                document.apply_operation(op);
            }
            last_applied_packet = expected_packet;
            pending_packets.erase(it);
            ++expected_packet;
            applied_any = true;
        }
        if (applied_any) {
            std::string content = document.get_content();
            buffer->text(content.c_str());
            display->redraw();
            window->redraw();
            log_message("Document updated with new content (len=" + std::to_string(content.size()) + ")");
        }
    }

    void log_message(const std::string& msg) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S") << " - " << msg;
        if (log_browser) {
            log_browser->add(oss.str().c_str());
            log_browser->bottomline(log_browser->size());
            log_browser->redraw();
        }
        fprintf(stderr, "GUI-LOG: %s\n", oss.str().c_str());
    }

    void run() {
        Fl::run();
    }
};

int main() {
    ReceiverApp app;
    app.run();
    return 0;
}
