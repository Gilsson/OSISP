#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_IP "127.0.0.1" // IP-адрес сервера
#define SERVER_PORT 27015       // Порт сервера
#define BUFFER_SIZE 1024       // Размер буфера для сообщений

SOCKET clientSocket;

void ReceiveMessages() {
	char buffer[BUFFER_SIZE];
	while (true) {
		int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
		if (bytesReceived > 0) {
			buffer[bytesReceived] = '\0'; // Добавляем нуль-терминатор
			std::cout << "\n\n" << buffer << "\n" << std::endl;
		}
		else if (bytesReceived == 0) {
			std::cout << "Connection closed by server." << std::endl;
			break;
		}
		else {
			std::cerr << "Error receiving data from server." << std::endl;
			break;
		}
	}
}

// Функция для отправки сообщений на сервер
void SendMessageToServer(const std::string& message) {
	int result = send(clientSocket, message.c_str(), message.size(), 0);
	if (result == SOCKET_ERROR) {
		std::cerr << "Error sending message: " << WSAGetLastError() << std::endl;
	}
}


// Основная функция клиента
int main() {
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cerr << "WSAStartup failed." << std::endl;
		return 1;
	}

	// Создаем сокет
	clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (clientSocket == INVALID_SOCKET) {
		std::cerr << "Socket creation failed. Error: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return 1;
	}

	// Настраиваем адрес сервера
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

	// Подключаемся к серверу
	if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		std::cerr << "Connection to server failed. Error: " << WSAGetLastError() << std::endl;
		closesocket(clientSocket);
		WSACleanup();
		return 1;
	}

	std::cout << "Connected to the server." << std::endl;

	// Запускаем поток для приема сообщений от сервера
	std::thread receiveThread(ReceiveMessages);
	receiveThread.detach();

	std::string command;
	while (true) {
		// Считываем команду пользователя
		std::cout << "Enter command (or '/exit' to quit): ";
		std::getline(std::cin, command);

		if (command == "/exit") {
			SendMessageToServer(command);
			break;
		}

		SendMessageToServer(command); // Отправляем команду на сервер
	}

	// Закрываем соединение и очищаем ресурсы
	closesocket(clientSocket);
	WSACleanup();
	return 0;
}
