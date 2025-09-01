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
#include <cstdlib>
#include <sstream>

const uint32_t MAX_PACKET_SIZE = 65536;
const uint32_t MAX_OPS_PER_PACKET = 40;
const uint32_t MAX_PACKET_BYTES = 8192;
const uint32_t BASE_BATCH_TIMEOUT_MS = 100;
const uint32_t HEARTBEAT_INTERVAL_MS = 1000;
const uint32_t CONNECTION_TIMEOUT_SEC = 10;

uint32_t htonl_safe(uint32_t x) { return htonl(x); }
uint64_t htonll(uint64_t x) { return ((uint64_t)htonl(x & 0xFFFFFFFF) << 32) | htonl(x >> 32); }

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

class HeadlessSender {
private:
    int sock_fd = -1;
    bool connected = false;
    std::string client_id;
    std::string target_ip;
    int target_port;
    
    uint64_t next_op_seq = 1;
    uint64_t next_packet_no = 1;
    std::vector<Operation> pending_ops;
    std::vector<std::vector<uint8_t>> send_queue;
    
    std::chrono::steady_clock::time_point last_heartbeat_sent;
    
public:
    HeadlessSender(const std::string& ip, int port) : target_ip(ip), target_port(port) {
        client_id = "web_sender_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 100000);
        std::cout << "HeadlessSender initialized with client_id: " << client_id << std::endl;
    }
    
    ~HeadlessSender() {
        if (sock_fd >= 0) {
            close(sock_fd);
        }
    }
    
    bool connect_to_receiver() {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(target_port);
        
        if (inet_pton(AF_INET, target_ip.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid IP address: " << target_ip << std::endl;
            close(sock_fd);
            sock_fd = -1;
            return false;
        }
        
        if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Failed to connect to " << target_ip << ":" << target_port << std::endl;
            close(sock_fd);
            sock_fd = -1;
            return false;
        }
        
        connected = true;
        last_heartbeat_sent = std::chrono::steady_clock::now();
        std::cout << "Connected to receiver at " << target_ip << ":" << target_port << std::endl;
        return true;
    }
    
    void send_text(const std::string& text) {
        if (!connected) {
            std::cerr << "Not connected to receiver" << std::endl;
            return;
        }
        
        // Create a replace operation (op_type 'R' replaces entire document)
        Operation op;
        op.op_type = 'R';
        op.op_seq = next_op_seq++;
        op.line = 0;
        op.col = 0;
        op.text = text;
        
        pending_ops.push_back(op);
        flush_operations();
        
        std::cout << "Sent text (length: " << text.length() << ")" << std::endl;
    }
    
    void run_with_stdin() {
        if (!connect_to_receiver()) {
            return;
        }
        
        // Read all text from stdin
        std::string input_text;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!input_text.empty()) {
                input_text += "\n";
            }
            input_text += line;
        }
        
        if (!input_text.empty()) {
            send_text(input_text);
            
            // Keep connection alive briefly to ensure delivery
            for (int i = 0; i < 3; ++i) {
                send_heartbeat();
                usleep(200000); // 0.2 seconds
            }
        } else {
            std::cout << "No input received from stdin" << std::endl;
        }
        
        std::cout << "Transmission completed" << std::endl;
    }
    
    void run_test() {
        if (!connect_to_receiver()) {
            return;
        }
        
        // Send some test messages
        send_text("Hello from headless sender!");
        usleep(1000000); // 1 second
        
        send_text("This is a test message.\nWith multiple lines.\nLine 3 here.");
        usleep(1000000); // 1 second
        
        send_text("Final test message: " + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        
        // Keep connection alive for a bit
        for (int i = 0; i < 10; ++i) {
            send_heartbeat();
            usleep(500000); // 0.5 seconds
        }
        
        std::cout << "Test completed" << std::endl;
    }
    
private:
    void flush_operations() {
        if (pending_ops.empty()) return;
        
        while (!pending_ops.empty()) {
            std::vector<Operation> batch;
            uint32_t batch_size = 0;
            
            while (!pending_ops.empty() && batch.size() < MAX_OPS_PER_PACKET) {
                const Operation& op = pending_ops.front();
                uint32_t op_size = 21 + op.text.size(); // Fixed header + text
                
                if (batch_size + op_size > MAX_PACKET_BYTES && !batch.empty()) {
                    break;
                }
                
                batch.push_back(op);
                batch_size += op_size;
                pending_ops.erase(pending_ops.begin());
            }
            
            if (!batch.empty()) {
                send_packet(batch);
            }
        }
    }
    
    void send_packet(const std::vector<Operation>& ops) {
        std::vector<uint8_t> packet_data;
        
        // Flags
        uint8_t flags = 0;
        packet_data.push_back(flags);
        
        // Packet number
        uint64_t packet_no_net = htonll(next_packet_no++);
        packet_data.insert(packet_data.end(), (uint8_t*)&packet_no_net, (uint8_t*)&packet_no_net + 8);
        
        // Client ID length and data
        uint32_t client_id_len_net = htonl_safe(client_id.length());
        packet_data.insert(packet_data.end(), (uint8_t*)&client_id_len_net, (uint8_t*)&client_id_len_net + 4);
        packet_data.insert(packet_data.end(), client_id.begin(), client_id.end());
        
        // Operations
        for (const Operation& op : ops) {
            packet_data.push_back(op.op_type);
            
            uint64_t op_seq_net = htonll(op.op_seq);
            packet_data.insert(packet_data.end(), (uint8_t*)&op_seq_net, (uint8_t*)&op_seq_net + 8);
            
            uint32_t line_net = htonl_safe(op.line);
            packet_data.insert(packet_data.end(), (uint8_t*)&line_net, (uint8_t*)&line_net + 4);
            
            uint32_t col_net = htonl_safe(op.col);
            packet_data.insert(packet_data.end(), (uint8_t*)&col_net, (uint8_t*)&col_net + 4);
            
            uint32_t text_len_net = htonl_safe(op.text.length());
            packet_data.insert(packet_data.end(), (uint8_t*)&text_len_net, (uint8_t*)&text_len_net + 4);
            packet_data.insert(packet_data.end(), op.text.begin(), op.text.end());
        }
        
        send_queue.push_back(packet_data);
        process_send_queue();
    }
    
    void process_send_queue() {
        while (!send_queue.empty() && connected) {
            const std::vector<uint8_t>& packet = send_queue.front();
            
            ssize_t bytes_sent = send(sock_fd, packet.data(), packet.size(), MSG_NOSIGNAL);
            if (bytes_sent < 0) {
                std::cerr << "Failed to send packet" << std::endl;
                connected = false;
                break;
            } else if ((size_t)bytes_sent != packet.size()) {
                std::cerr << "Partial send: " << bytes_sent << "/" << packet.size() << std::endl;
            }
            
            send_queue.erase(send_queue.begin());
        }
    }
    
    void send_heartbeat() {
        if (!connected) return;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_sent).count();
        
        if (elapsed >= HEARTBEAT_INTERVAL_MS) {
            // Send empty packet as heartbeat
            std::vector<Operation> empty_ops;
            send_packet(empty_ops);
            last_heartbeat_sent = now;
        }
    }
};

int main(int argc, char* argv[]) {
    std::string host = "localhost";
    int port = 10000;
    bool test_mode = false;
    
    if (argc >= 2) {
        host = argv[1];
    }
    
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port number. Using default port 10000." << std::endl;
            port = 10000;
        }
    }
    
    if (argc >= 4 && std::string(argv[3]) == "test") {
        test_mode = true;
    }
    
    std::cout << "Starting headless sender, connecting to " << host << ":" << port << std::endl;
    
    HeadlessSender sender(host, port);
    
    if (test_mode) {
        sender.run_test();
    } else {
        sender.run_with_stdin();
    }
    
    return 0;
}
