#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h> 
#include <vector>
#include <string>
#include <sstream>


std::vector<std::string> parse_resp0(std::string &data){
  //"*2\r\n$4\r\nECHO\r\n$3\r\nhey\r\n"
  auto split = [&](const std::string delim){
    std::vector<std::string> result;
    size_t start = 0;

    while(true){
      size_t pos = data.find(delim, start);
      if(pos == std::string::npos){
        result.push_back(data.substr(start));
        break;
      }
      result.push_back(data.substr(start, pos-start));
      start = pos + delim.size();
    }
    return result;
  };
  std::vector<std::string> lines = split("\r\n");
  std::vector<std::string> parsed_commands;
  for(size_t i=0; i < lines.size(); i++){
    if(lines[i].empty()) continue;
    if(lines[i][0] == '$'){
      if(i+1 < lines.size()){
        parsed_commands.push_back(lines[i+1]);
        i++;
      }
    }
  }

  return parsed_commands;

}

std::vector<std::string> parse_resp(const std::string &data) {
    std::vector<std::string> commands;
    size_t pos = 0;

    // 1. A valid client request always starts with an Array (*)
    if (data.empty() || data[0] != '*') return commands;

    // Find the end of the *2\r\n line
    pos = data.find("\r\n", pos);
    if (pos == std::string::npos) return commands;
    pos += 2; // Jump past the \r\n

    // 2. Loop through the rest of the string
    while (pos < data.length()) {
        
        // If we see a Bulk String length indicator...
        if (data[pos] == '$') {
            
            // Find where the length number ends
            size_t end_of_length = data.find("\r\n", pos);
            if (end_of_length == std::string::npos) break;

            // Extract the actual number (e.g., "$4" -> 4)
            std::string length_str = data.substr(pos + 1, end_of_length - pos - 1);
            int word_length = std::stoi(length_str);

            // Move our position to the start of the actual word (curr end_of_length is index of "\r", we want to skip "\n" and go to the next start char)
            pos = end_of_length + 2;

            // Extract EXACTLY 'word_length' bytes (This makes it binary safe!)
            if (pos + word_length <= data.length()) {
                commands.push_back(data.substr(pos, word_length));
            }

            // Advance our position past the word and its trailing \r\n
            pos = pos + word_length + 2;
        } else {
            // If it's not a bulk string, we break for now.
            break; 
        }
    }

    return commands;
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  // initialize event loop watchlist
  std::vector<struct pollfd> fds;

  // add server sockets to watchlist

  struct pollfd server_pollfd;
  server_pollfd.fd = server_fd;
  server_pollfd.events = POLLIN; // watch for incoming data/connections
  fds.push_back(server_pollfd);
  
  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";

  
  // use buffer and recv() in while loop (one client can send multiple PINGs
  // client sends multiple commands over one connection, 
  //they don't necessarily arrive one by one; they arrive as a continuous "stream" of bytes.
  while(true){

    // wait for IS to tell a socket is ready
    int ready_count = poll(fds.data(), fds.size(), -1);
    
    if (ready_count < 0){
      std::cerr << "Poll failed\n";
      break;
    }

    // loop through watched sockets to see which ones woke us up
    for (size_t i = 0; i < fds.size(); i++){
      // if revents is 0, nothing happened in this socket
      if(fds[i].revents == 0) continue;

      // check if POLLIN, using the bit manipulation
      if(fds[i].revents & POLLIN){

        if(fds[i].fd == server_fd){
          // case 1: server socket is read, new client trying to connect
          struct sockaddr_in client_addr;
          int client_addr_len = sizeof(client_addr);
          int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
          if(client_fd >= 0){
            // add new client to our watchlist
            struct pollfd client_pollfd;
            client_pollfd.fd = client_fd;
            client_pollfd.events = POLLIN;
            fds.push_back(client_pollfd);
          }
          
        }else{
          // case 2: client socket is ready (or there's an error)
          char buffer[1024];
          int bytes_recevied = recv(fds[i].fd, buffer,sizeof(buffer),0);

          // 0 means disconnect, negative means error 
          if (bytes_recevied <= 0){ 
            close(fds[i].fd);

            // swap and pop for O(1) removal from vector
            fds[i] = fds.back();
            fds.pop_back();
            // Decrement 'i' because we just swapped a new element 
            // into the current index, and we need to check it.
            i--;
          }else{
            // received data
            std::string client_buffer(buffer, bytes_recevied);
            std::vector<std::string> commands = parse_resp(client_buffer);

            if(commands.size() > 0){
              std::string cmd = commands[0];
              if(cmd == "PING" || cmd == "ping"){
                const char *response = "+PONG\r\n";
                send(fds[i].fd, response, strlen(response),0);
              }else if(cmd == "ECHO" || cmd == "echo"){
                if(commands.size() > 1){
                  std::string msg = commands[1]; // extract message
                  //rebuild dynamic resp bulk string 
                  std::string response = "$" + std::to_string(msg.length()) + "\r\n" + msg + "\r\n";
                  // Send it back! (Use .c_str() to convert std::string to const char*)
                  send(fds[i].fd, response.c_str(), response.length(), 0);
                }
              }
            }

          }
        }
      }
    }
  }

  close(server_fd);

  return 0;
}
