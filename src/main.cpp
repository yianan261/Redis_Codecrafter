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
            // received data, we only need to fire PONG here
            const char *response = "+PONG\r\n";
            send(fds[i].fd, response, strlen(response),0);
          }
        }
      }
    }
  }

  close(server_fd);
  

  return 0;
}
