#include <winsock2.h>  
#include <ws2tcpip.h>  
#include <iostream>  
#include <string>   
#include <omp.h>
#include <vector>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")  
#pragma comment(lib, "iphlpapi.lib")

struct SubnetInfo {
    std::string ip;
    std::string mask;
    std::string baseIp;
};

struct SubnetRange {
    unsigned int firstIP;
    unsigned int lastIP;
    unsigned int numHosts;
};

SubnetRange calculateSubnetRange(const std::string& ipAddress, const std::string& subnetMask) {
    SubnetRange range;

    struct in_addr ip_addr, mask_addr;
    inet_pton(AF_INET, ipAddress.c_str(), &ip_addr);
    inet_pton(AF_INET, subnetMask.c_str(), &mask_addr);

    unsigned int ip = ntohl(ip_addr.s_addr);
    unsigned int mask = ntohl(mask_addr.s_addr);

    unsigned int network = ip & mask;

    unsigned int broadcast = network | (~mask);

    range.firstIP = network + 1;

    range.lastIP = broadcast - 1;

    range.numHosts = range.lastIP - range.firstIP + 1;

    return range;
}


SubnetInfo detectSubnet() {
    SubnetInfo result;
    ULONG bufferSize = 15000;
    IP_ADAPTER_ADDRESSES* adapterAddresses = nullptr;

    // Alloca memoria per la struttura IP_ADAPTER_ADDRESSES
    adapterAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufferSize);
    if (!adapterAddresses) {
        std::cerr << "Errore nell'allocazione della memoria" << std::endl;
        return result;
    }

    // Ottieni le informazioni sulle interfacce di rete
    ULONG retVal = GetAdaptersAddresses(
        AF_INET,                // Solo IPv4
        GAA_FLAG_INCLUDE_PREFIX,  // Includi informazioni sul prefisso
        NULL,                   // Parametro riservato
        adapterAddresses,       // Buffer di output
        &bufferSize             // Dimensione del buffer
    );

    if (retVal != ERROR_SUCCESS) {
        std::cerr << "Errore nell'ottenere le informazioni sugli adattatori di rete: " << retVal << std::endl;
        free(adapterAddresses);
        return result;
    }

    // Scorrere la lista degli adattatori e trovare una connessione attiva
    std::vector<SubnetInfo> subnetInfos;

    #pragma omp parallel
    {
        std::vector<SubnetInfo> localSubnetInfos;

        #pragma omp for nowait
        for (IP_ADAPTER_ADDRESSES* adapter = adapterAddresses; adapter != nullptr; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;

            // Scorrere gli indirizzi IP dell'adattatore
            for (IP_ADAPTER_UNICAST_ADDRESS* address = adapter->FirstUnicastAddress; address != nullptr; address = address->Next) {
                if (address->Address.lpSockaddr->sa_family == AF_INET) {
                    SubnetInfo localResult;

                    // Converti l'indirizzo in formato stringa
                    sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr);
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(ipv4->sin_addr), ipStr, INET_ADDRSTRLEN);
                    localResult.ip = std::string(ipStr);

                    // Ottieni la subnet mask
                    ULONG mask = (0xFFFFFFFF << (32 - address->OnLinkPrefixLength)) & 0xFFFFFFFF;
                    sockaddr_in subnetMask;
                    subnetMask.sin_addr.s_addr = htonl(mask);
                    inet_ntop(AF_INET, &(subnetMask.sin_addr), ipStr, INET_ADDRSTRLEN);
                    localResult.mask = std::string(ipStr);

                    // Calcola l'indirizzo di rete (base della subnet)
                    struct in_addr network;
                    network.s_addr = ipv4->sin_addr.s_addr & subnetMask.sin_addr.s_addr;
                    inet_ntop(AF_INET, &network, ipStr, INET_ADDRSTRLEN);

                    // Costruisci la base IP per lo scanning (es. "192.168.1.")
                    std::string baseIp = std::string(ipStr);
                    baseIp = baseIp.substr(0, baseIp.rfind('.') + 1);
                    localResult.baseIp = baseIp;

                    localSubnetInfos.push_back(localResult);
                }
            }
        }

        #pragma omp critical
        subnetInfos.insert(subnetInfos.end(), localSubnetInfos.begin(), localSubnetInfos.end());
    }

    free(adapterAddresses);

    // Restituisci il primo risultato valido
    if (!subnetInfos.empty()) {
        return subnetInfos[0];
    }

    // Se non viene trovata nessuna interfaccia attiva, usa un valore predefinito
    result.ip = "127.0.0.1";
    result.mask = "255.0.0.0";
    result.baseIp = "192.168.1.";

    return result;
}



bool isPortOpen(const std::string& ip, int port, int timeoutMs = 50) {  
   SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);  
   if (sock == INVALID_SOCKET) {  
       std::cerr << "Errore nella creazione del socket: " << WSAGetLastError() << std::endl;  
       return false;  
   }  
   sockaddr_in addr;  
   memset(&addr, 0, sizeof(addr));  
   addr.sin_family = AF_INET;  
   addr.sin_port = htons(port);  

   if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {  
       std::cerr << "Errore nell'inizializzazione dell'indirizzo IP: " << ip << std::endl;  
       closesocket(sock);  
       return false;  
   }  

   u_long mode = 1;  
   if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {  
       std::cerr << "Errore nell'impostazione del socket non bloccante: " << WSAGetLastError() << std::endl;  
       closesocket(sock);  
       return false;  
   }  

   int result = connect(sock, (sockaddr*)&addr, sizeof(addr));  
   if (result == SOCKET_ERROR) {  
       int error = WSAGetLastError();  
       if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) {  
           std::cerr << "Errore nella connessione a " << ip << ":" << port  
               << " - Codice errore: " << error << std::endl;  
           closesocket(sock);  
           return false;  
       }  
   }  

   fd_set writefds;  
   FD_ZERO(&writefds);  
   FD_SET(sock, &writefds);  

   timeval timeout = {};  
   timeout.tv_sec = timeoutMs / 1000;  
   timeout.tv_usec = (timeoutMs % 1000) * 1000;  

   int sel = select(0, NULL, &writefds, NULL, &timeout);  
   closesocket(sock);  

   if (sel <= 0) {  
       return false;  
   }  

   return true;  
}  

std::string resolveHost(const std::string& ip) {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 0;

    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        return "Indirizzo IP non valido";
    }

    char host[NI_MAXHOST];
    // Passare l'indirizzo corretto con il tipo giusto
    if (getnameinfo((sockaddr*)&addr, sizeof(addr), host, sizeof(host), NULL, 0, NI_NAMEREQD) == 0) {
        return std::string(host);
    }
    else {
        int error = WSAGetLastError();
        return "Nome non risolto (Errore: " + std::to_string(error) + ")";
    }
}



int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
	SubnetInfo subnetInfo = detectSubnet();
    std::string baseIp = subnetInfo.baseIp;  //sostituire dinamicamente con subnet rilevata  
	std::string subnetMask = subnetInfo.mask; //maschera di sottorete
	std::string ip = subnetInfo.ip; //indirizzo IP locale
    int port = 4840;
	SubnetRange range = calculateSubnetRange(ip, subnetMask);
    int numberOfHosts = range.numHosts;
    // Vector per memorizzare i risultati della scansione
    std::vector<std::string> results;

    // Mutex per proteggere l'accesso al vector da più thread
    omp_lock_t writeLock;
    omp_init_lock(&writeLock);

    // Parallelizzazione del ciclo for
#pragma omp parallel for
    for (int i = 1; i <= numberOfHosts; ++i) {
        std::string ip = baseIp + std::to_string(i);
        if (isPortOpen(ip, port)) {
            std::string nome = resolveHost(ip);
            std::string result = ip + " - " + nome;

            // Proteggi l'accesso al vector condiviso
            omp_set_lock(&writeLock);
            results.push_back(result);
            omp_unset_lock(&writeLock);
        }
    }

    omp_destroy_lock(&writeLock);

    // Stampa i risultati alla fine
    for (const auto& result : results) {
        std::cout << result << std::endl;
    }

    WSACleanup();
    return 0;
}
