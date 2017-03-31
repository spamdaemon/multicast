#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/** Includes needed for the sockets */
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

static const int SOCKET_TYPE = SOCK_DGRAM;
static const int SOCKET_PROTOCOL = IPPROTO_UDP;
static const int TTL = 1;
static const char* DEFAULT_PORT = "12345";
static const char* DEFAULT_MC_ADDRESS = "224.0.0.1";

/**
 * Parse an address and port of the form : <ipv4address>[:[<port>]]
 *
 * @param addressport
 * @param ip_addrlen the size of the retgurned structure
 * @param ip_port the parsed port
 * @return an ipaddr structure or NULL on error
 */
static struct sockaddr* parse_address(const char* addressport, int* ip_domain, socklen_t* ip_addrlen)
{
   char* rawAddress = strdup(addressport == NULL ? DEFAULT_MC_ADDRESS : addressport);
   const char* service = DEFAULT_PORT;
   char* address = rawAddress;
   const char* separators = "/:";

   // split out address and service
   if (rawAddress[0] == '[') {
      // ipv6 address
      address = rawAddress + 1;
      char* x = rindex(rawAddress, ']');
      if (x != NULL) {
         *x = '\0';
         x = rindex(x + 1, ':');
         if (x != NULL) {
            *x = '\0';
            service = x + 1;
         }
      }
   }
   else
      for (const char* sep = separators; *sep != '\0'; ++sep) {
         char* x = rindex(address, *sep);
         if (x != NULL) {
            *x = '\0';
            service = x + 1;
            break;
         }
      }

   struct addrinfo hints = {
   AI_NUMERICHOST,
   AF_UNSPEC, SOCKET_TYPE, SOCKET_PROTOCOL };
   struct addrinfo* addresses = NULL;
   struct sockaddr* ip_addr = NULL;

   int error = getaddrinfo(address, service, &hints, &addresses);

   if (error) {
      if (addressport == NULL) {
         fprintf(stderr, "Something unexpected happened : %s\n", gai_strerror(error));
      }
      else {
         fprintf(stderr, "Failed to parse address %s : %s\n", addressport, gai_strerror(error));
      }
      goto CLEANUP;
   }

   for (struct addrinfo* a = addresses; a != NULL; a = a->ai_next) {
      if (a->ai_family == AF_INET || a->ai_family == AF_INET6) {
         ip_addr = (struct sockaddr*) malloc(a->ai_addrlen);
         if (ip_addr != NULL) {
            *ip_domain = a->ai_family;
            *ip_addrlen = a->ai_addrlen;
            memcpy(ip_addr, a->ai_addr, a->ai_addrlen);
            break;
         }
      }
   }

   CLEANUP:

   if (ip_addr != NULL) {
      fprintf(stderr, "Found an address for %s/%s\n", address, service);
   }
   else {
      fprintf(stderr, "No address found for %s/%s\n", address, service);
   }
   free(rawAddress);
   freeaddrinfo(addresses);
   return ip_addr;
}

static int bindAny(int s, struct sockaddr* ip_addr, socklen_t ip_addrlen)
{
   if (ip_addr->sa_family == AF_INET) {
      struct sockaddr_in addr;
      addr.sin_addr.s_addr = INADDR_ANY;
      addr.sin_port = 0;
      addr.sin_family = ip_addr->sa_family;
      return bind(s, (struct sockaddr*) &addr, sizeof(addr));
   }
   else if (ip_addr->sa_family == AF_INET6) {
      struct sockaddr_in6 addr;
      addr.sin6_addr = in6addr_any;
      addr.sin6_port = 0;
      addr.sin6_flowinfo = 0;
      addr.sin6_scope_id = 0;
      addr.sin6_family = ip_addr->sa_family;
      return bind(s, (struct sockaddr*) &addr, sizeof(addr));
   }
   return 1;
}

static int set_SO_REUSEADDR(int s)
{
   int value = 1;
   return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
}

static int configureMulticastReader(int s, struct sockaddr* ip_addr, socklen_t ip_addrlen)
{
   if (ip_addr->sa_family == AF_INET) {
      struct sockaddr_in* in_addr = (struct sockaddr_in*) ip_addr;
      struct ip_mreq mreq = { in_addr->sin_addr, INADDR_ANY };
      if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
         return 1;
      }
   }
   else if (ip_addr->sa_family == AF_INET6) {
      struct sockaddr_in6* in6_addr = (struct sockaddr_in6*) ip_addr;
      struct ipv6_mreq mreq = { in6_addr->sin6_addr, 0 };
      if (setsockopt(s, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
         return 1;
      }
   }
   else {
      return 1;
   }
   return 0;
}

static int configureMulticastWriter(int s, struct sockaddr* ip_addr, socklen_t ip_addrlen, int ttl)
{
   int mc_loop = 1;
   struct in_addr interface_addr;

   if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0) {
      return 1;
   }

   if (ip_addr->sa_family == AF_INET) {
      if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &mc_loop, sizeof(mc_loop)) != 0) {
         return 1;
      }
      /*
       if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr)) != 0) {
       return 1;
       }
       */
   }
   else if (ip_addr->sa_family == AF_INET6) {
      if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &mc_loop, sizeof(mc_loop)) != 0) {
         return 1;
      }
      /*
       if (setsockopt(s, IPPROTO_IP, IP6_MULTICAST_IF, &interface_addr, sizeof(interface_addr)) != 0) {
       return 1;
       }
       */
   }
   else {
      return 1;
   }
   return 0;
}

static void usage(int argc, char* const * argv)
{
   fprintf(stderr,
         "Usage: %s [-h] [-t <ttl>] [-n <num>] [-b] [-s <interval>] [-m <message>] [-r] [send|receive] [<address>:<port>]\n",
         argv[0]);
   fprintf(stderr, " Send or a receive a message on the specified multicast address\n");
   fprintf(stderr, " The default address is %s, and the default port is %s\n", DEFAULT_MC_ADDRESS, DEFAULT_PORT);
   fprintf(stderr, " Ports can be separated by a / or : from the address. If the address is a IPv6 address,\n");
   fprintf(stderr, " then it must be enclosed in [] if the : separator is used\n");
   fprintf(stderr, "Options:\n");
   fprintf(stderr, "   -h     this message\n");
   fprintf(stderr, "   -t     the TTL when sending message (default: %d)\n", TTL);
   fprintf(stderr, "   -n     number of messages to send or receive (default: -1 for infinite)\n");
   fprintf(stderr, "   -m     the message to send\n");
   fprintf(stderr,
         "   -s     the number of seconds between sending messages; must non-negative integer (default: 1)\n");
   fprintf(stderr, "   -r     read a message from stdin, one line at a time unless binary\n");
   fprintf(stderr, "   -b     data will be binary\n");
   fprintf(stderr, "Examples:\n");
   fprintf(stderr, " %s -t 4 send 224.0.0.2/12345\n", argv[0]);
   fprintf(stderr, " %s -s 2 -mFoo send FF05:0:0:0:0:0:0:2/55555\n", argv[0]);
   fprintf(stderr, " %s receive 224.0.0.2/12345\n", argv[0]);
   fprintf(stderr, " %s [FF05:0:0:0:0:0:0:2]:55555\n", argv[0]);
   exit(1);
}

int main(int argc, char* const * argv)
{
   if (argc < 1) {
      usage(argc, argv);
   }
   const char* message = "Hello, World";
   ssize_t messageLen = strlen(message);

   int isWriter = 0;
   int ttl = TTL;
   long long nMessages = -1;
   int sleepSec = 1;
   FILE* readMessage = NULL;
   size_t bufSize = 0;
   char* buf = 0;
   int binary = 0;

   optind = 1;
   for (int opt; (opt = getopt(argc, argv, "ht:m:n:s:rb")) != -1;) {
      switch (opt) {
         case 't':
            ttl = atol(optarg);
            if (ttl < 0) {
               fprintf(stderr, "Invalid ttl %s\n", optarg);
               exit(1);
            }
            isWriter = 1;
            break;
         case 'm':
            message = optarg;
            messageLen = strlen(message);
            isWriter = 1;
            break;
         case 'r':
            message = NULL;
            messageLen = 0;
            readMessage = stdin;
            isWriter = 1;
            break;
         case 'b':
            binary = 1;
            break;
         case 'n':
            nMessages = atoi(optarg);
            break;
         case 's':
            sleepSec = atoi(optarg);
            if (sleepSec < 0) {
               fprintf(stderr, "Invalid sleep value specified\n");
               usage(argc, argv);
            }
            isWriter = 1;
            break;
         default:
            usage(argc, argv);
            break;
      }
   }

   const char* errorMessage = NULL;
   int sock = -1;
   struct sockaddr* ip_addr = NULL;

   int argPos = optind;

   if (argc > argPos) {
      if (strcmp(argv[argPos], "send") == 0) {
         ++argPos;
         isWriter = 1;
      }
      else if (strcmp(argv[argPos], "receive") == 0) {
         ++argPos;
         isWriter = 0;
      }
   }
   socklen_t ip_addrlen;
   int ip_family = 0;
   ip_addr = parse_address(argc > argPos ? argv[argPos] : NULL, &ip_family, &ip_addrlen);
   if (ip_addr == NULL) {
      errorMessage = "Failed to parse the address and/or port";
      goto CLEANUP;
   }
   // open the socket
   sock = socket(ip_family, SOCKET_TYPE, SOCKET_PROTOCOL);
   if (sock < 0) {
      errorMessage = "Failed to create a socket";
      goto CLEANUP;
   }

   // bind the socket
   if (isWriter) {
      if (configureMulticastWriter(sock, ip_addr, ip_addrlen, ttl) != 0) {
         errorMessage = "Failed to configure multicast";
         goto CLEANUP;
      }

      if (bindAny(sock, ip_addr, ip_addrlen) != 0) {
         errorMessage = "Failed to bind socket";
         goto CLEANUP;
      }

      if (connect(sock, ip_addr, ip_addrlen) != 0) {
         errorMessage = "Failed to connect socket";
         goto CLEANUP;
      }

      if (readMessage!=NULL && binary==1) {
         bufSize = 68000;
         buf = malloc(bufSize);
         messageLen = 0;
         while (feof(readMessage) == 0 && (bufSize - messageLen) > 0) {
            ssize_t actual = fread(buf + messageLen, 1,bufSize - messageLen,  readMessage);
            if (actual > 0) {
               messageLen += actual;
            }
            if (ferror(readMessage) != 0) {
               break;
            }
         }

         send(sock, buf, messageLen, 0);
         nMessages = 0;
      }

      while (nMessages < 0 || nMessages > 0) {

         // read message first
         if (readMessage != NULL) {
            messageLen = getline(&buf, &bufSize, readMessage);
            if (messageLen < 0) {
               break;
            }
            message = buf;
         }

         ssize_t actual = send(sock, message, messageLen, 0);
         if (actual < 0) {
            perror("Failed to send message");
            break;
         }

         --nMessages;
         if (nMessages != 0 && sleepSec > 0) {
            sleep(sleepSec);
         }
      }
   }
   else {
      // set some socket options
      if (set_SO_REUSEADDR(sock)) {
         errorMessage = "Failed to set socket option SO_REUSEADDR";
         goto CLEANUP;
      }

      if (configureMulticastReader(sock, ip_addr, ip_addrlen) != 0) {
         errorMessage = "Failed to configure multicast";
         goto CLEANUP;
      }
      if (bind(sock, ip_addr, ip_addrlen) != 0) {
         errorMessage = "Failed to bind socket";
         goto CLEANUP;
      }

      // start reading from the socket
      bufSize = 1500;
      buf = malloc(bufSize);
      while (nMessages < 0 || nMessages > 0) {
         ssize_t n = recv(sock, buf, bufSize - 1, 0);
         if (n < 0) {
            break;
         }
         if (binary) {
            fwrite(buf, 1,bufSize,  stdout);
         }
         else {
            buf[n] = '\0';
            // don't just print buf, because if might contain % escapes!!
            fprintf(stdout, "%s", buf);
            if (n > 0 && buf[n - 1] != '\n') {
               fprintf(stdout, "\n");
            }
         }
         fflush(stdout);
         --nMessages;
      }
   }

   errno = 0;

   CLEANUP: free(buf);
   free(ip_addr);

   if (errno != 0) {
      errorMessage = strerror(errno);
   }

   if (sock >= 0) {
      close(sock);
   }

   if (errorMessage != NULL) {
      fprintf(stderr, "%s\n", errorMessage);
      usage(argc, argv);
   }
   return 0;
}

