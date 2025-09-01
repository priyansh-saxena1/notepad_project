#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <cstring>
#include <iostream>
#include <cstdio>
#include <cstdlib>

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
                // Out of bounds, ignore
            } else {
                if (op.text.find('\n') == std::string::npos) {
                    std::string &target_line = lines[op.line];
                    size_t start_pos = std::min((size_t)op.col, target_line.length());
                    size_t delete_len = std::min(op.text.length(), target_line.length() - start_pos);
                    if (start_pos < target_line.length()) target_line.erase(start_pos, delete_len);
                } else {
                    // Multi-line delete logic (simplified)
                    std::string &first_line = lines[op.line];
                    size_t start_pos = std::min((size_t)op.col, first_line.length());
                    first_line.erase(start_pos);
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

class HeadlessReceiver {
private:
    int server_fd = -1;
    int client_fd = -1;
    int port = 10000;
    
    std::map<uint64_t, Packet> pending_packets;
    uint64_t expected_packet = 1;
    uint64_t last_applied_packet = 0;
    
    DocumentModel document;
    std::chrono::steady_clock::time_point last_heartbeat;
    
public:
    HeadlessReceiver(int p = 10000) : port(p) {
        setup_server();
    }
    
    ~HeadlessReceiver() {
        if (client_fd >= 0) close(client_fd);
        if (server_fd >= 0) close(server_fd);
    }
    
    bool setup_server() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set socket options" << std::endl;
            return false;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);
        
        if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Failed to bind to port " << port << std::endl;
            return false;
        }
        
        if (listen(server_fd, 1) < 0) {
            std::cerr << "Failed to listen on socket" << std::endl;
            return false;
        }
        
        std::cout << "Receiver listening on port " << port << std::endl;
        return true;
    }
    
    void run() {
        while (true) {
            if (client_fd < 0) {
                accept_connection();
            }
            
            if (client_fd >= 0) {
                handle_client_data();
                check_heartbeat();
            }
            
            usleep(30000); // 30ms sleep
        }
    }
    
private:
    void accept_connection() {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms
        
        int activity = select(server_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity > 0 && FD_ISSET(server_fd, &read_fds)) {
            client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd >= 0) {
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                std::cout << "Client connected from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;
                last_heartbeat = std::chrono::steady_clock::now();
            }
        }
    }
    
    void handle_client_data() {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10ms
        
        int activity = select(client_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity > 0 && FD_ISSET(client_fd, &read_fds)) {
            std::vector<uint8_t> buffer(MAX_PACKET_SIZE);
            ssize_t bytes_received = recv(client_fd, buffer.data(), buffer.size(), 0);
            
            if (bytes_received <= 0) {
                std::cout << "Client disconnected" << std::endl;
                close(client_fd);
                client_fd = -1;
                document.reset();
                expected_packet = 1;
                pending_packets.clear();
            } else {
                buffer.resize(bytes_received);
                Packet packet = parse_packet(buffer);
                process_packet(packet);
            }
        }
    }
    
    void check_heartbeat() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat).count();
        if (elapsed > HEARTBEAT_TIMEOUT_MS) {
            std::cout << "Heartbeat timeout - connection lost" << std::endl;
            if (client_fd >= 0) {
                close(client_fd);
                client_fd = -1;
            }
            document.reset();
            expected_packet = 1;
            pending_packets.clear();
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
            op.op_seq = ntohll(op.op_seq); 
            pos += 8;
            
            if (pos + 4 > data.size()) break;
            memcpy(&op.line, data.data() + pos, 4); 
            op.line = ntohl_safe(op.line); 
            pos += 4;
            
            if (pos + 4 > data.size()) break;
            memcpy(&op.col, data.data() + pos, 4); 
            op.col = ntohl_safe(op.col); 
            pos += 4;
            
            uint32_t text_len;
            if (pos + 4 > data.size()) break;
            memcpy(&text_len, data.data() + pos, 4); 
            text_len = ntohl_safe(text_len); 
            pos += 4;
            
            if (pos + text_len > data.size()) break;
            op.text = std::string((char*)data.data() + pos, text_len); 
            pos += text_len;
            
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
            std::cout << "Document updated. Content length: " << content.size() << std::endl;
            std::cout << "Current content:\n" << content << std::endl;
            std::cout << "---" << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    int port = 10000;
    
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port number. Using default port 10000." << std::endl;
            port = 10000;
        }
    }
    
    std::cout << "Starting headless receiver on port " << port << std::endl;
    
    HeadlessReceiver receiver(port);
    receiver.run();
    
    return 0;
}
