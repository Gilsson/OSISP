#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>
#include <set>

#pragma comment(lib, "Ws2_32.lib")

const int MAX_CLIENTS = 10;
#define BUFFER_SIZE 1024
#define PORT 27015
HANDLE clientMutex;
HANDLE chatMutex;
HANDLE serverMutex;


struct ClientInfo {
	SOCKET clientSocket;
	int clientID;
	std::set<int> activeChatIDs;
};

struct ChatRoom {
	int chatID;                             // Уникальный ID чата
	std::set<int> participants;             // Множество участников (clientID), подключенных к чату
};

// Массив для хранения всех клиентов
std::unordered_map<int, ClientInfo> clients;
std::unordered_map<int, ChatRoom> chatRooms;

void SendMessageToClient(SOCKET clientSocket, const std::string& message) {
	send(clientSocket, message.c_str(), message.size(), 0);
}

int SendPrivateMessage(int senderID, int receiverID, const std::string& message) {
	int out = 0;
	WaitForSingleObject(clientMutex, INFINITE);
	if (clients.find(receiverID) != clients.end()) {
		SOCKET receiverSocket = clients[receiverID].clientSocket;
		std::string fullMessage = "Private from " + std::to_string(senderID) + ": " + message;
		send(receiverSocket, fullMessage.c_str(), fullMessage.size(), 0);
	}
	else {
		out = 1;
	}
	ReleaseMutex(clientMutex);
	return out;
}

void SendMessageToChat(int senderID, int chatID, const std::string& message) {
	WaitForSingleObject(chatMutex, INFINITE);

	// Проверяем, существует ли указанный чат
	if (chatRooms.find(chatID) != chatRooms.end()) {
		// Проверяем, что отправитель является участником чата
		auto& participants = chatRooms[chatID].participants;
		if (std::find(participants.begin(), participants.end(), senderID) != participants.end()) {
			// Отправляем сообщение всем участникам чата
			for (int clientID : participants) {
				WaitForSingleObject(clientMutex, INFINITE);
				SOCKET clientSocket = clients[clientID].clientSocket;
				ReleaseMutex(clientMutex);
				send(clientSocket, message.c_str(), message.size(), 0);
			}
		}
		else {
			// Если отправитель не в чате, сообщаем об ошибке
			WaitForSingleObject(clientMutex, INFINITE);
			SOCKET senderSocket = clients[senderID].clientSocket;
			ReleaseMutex(clientMutex);
			std::string errorMessage = "Error: You are not a member of chat " + std::to_string(chatID) + ".";
			send(senderSocket, errorMessage.c_str(), errorMessage.size(), 0);
		}
	}
	else {
		// Чат не найден, сообщаем отправителю
		WaitForSingleObject(clientMutex, INFINITE);
		SOCKET senderSocket = clients[senderID].clientSocket;
		ReleaseMutex(clientMutex);
		std::string errorMessage = "Error: Chat " + std::to_string(chatID) + " does not exist.";
		send(senderSocket, errorMessage.c_str(), errorMessage.size(), 0);
	}

	ReleaseMutex(chatMutex);
}

int ExtractClientID(const std::string& message) {
	if (message.substr(0, 2) == "To") {
		if (!std::isdigit(message[2])) {
			return -1; // Возвращаем -1, если символ не является числом
		}

		try {
			// Извлекаем и возвращаем ID клиента
			return std::stoi(message.substr(2, 1));
		}
		catch (const std::exception&) {
			return -1; // Возвращаем -1, если преобразование не удалось
		}
	}
	return -1; // Возвращаем -1, если префикс не обнаружен
}

int ExtractChatID(const std::string& message) {
	if (message.substr(0, 4) == "Chat") {
		if (!std::isdigit(message[4])) {
			return -1; // Возвращаем -1, если символ не является числом
		}

		try {
			// Извлекаем и возвращаем ID клиента
			return std::stoi(message.substr(4, 1));
		}
		catch (const std::exception&) {
			return -1; // Возвращаем -1, если преобразование не удалось
		}
	}
	return -1; // Возвращаем -1, если префикс не обнаружен
}

int LeaveChatRoom(int clientID, int chatID) {
	int out = 0;
	WaitForSingleObject(clientMutex, INFINITE);
	WaitForSingleObject(chatMutex, INFINITE);

	// Проверяем, что клиент существует и чат с таким ID существует
	if (clients.find(clientID) != clients.end() && chatRooms.find(chatID) != chatRooms.end()) {
		// Проверяем, что клиент является участником данного чата
		auto& participants = chatRooms[chatID].participants;
		auto participantIt = participants.find(clientID);

		if (participantIt != participants.end()) {
			// Удаляем клиента из участников чата
			participants.erase(participantIt);
			// Удаляем ID чата из активных чатов клиента
			clients[clientID].activeChatIDs.erase(chatID);

			// Если чат пуст, удаляем его
			if (participants.empty()) {
				chatRooms.erase(chatID);
			}
		}
		else {
			// Клиент не является участником чата
			std::cerr << "Error: Client " << clientID << " is not a member of chat " << chatID << "." << std::endl;
			out = 1;
		}
	}
	else {
		std::cerr << "Error: Client or chat does not exist." << std::endl;
		out = 2;
	}

	ReleaseMutex(chatMutex);
	ReleaseMutex(clientMutex);
	return out;
}


void DisconnectClient(int clientID) {
	WaitForSingleObject(clientMutex, INFINITE);
	if (clients.find(clientID) != clients.end()) {
		for (int chatID : clients[clientID].activeChatIDs) {
			LeaveChatRoom(clientID, chatID);
		}
		closesocket(clients[clientID].clientSocket);
		clients.erase(clientID);
	}
	ReleaseMutex(clientMutex);
}


int GenerateChatID() {
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (chatRooms.find(i) == chatRooms.end()) {
			return i; // Возвращаем первый свободный ID
		}
	}
	return -1; // Нет доступного ID
}

int CreateChatRoom() {
	int chatID = GenerateChatID();
	if (chatID != -1) {
		WaitForSingleObject(chatMutex, INFINITE);
		chatRooms[chatID] = { chatID, {} }; // Создаем новый чат
		ReleaseMutex(chatMutex);
	}
	return chatID;
}

bool JoinChatRoom(int clientID, int chatID) {
	WaitForSingleObject(clientMutex, INFINITE);
	WaitForSingleObject(chatMutex, INFINITE);

	if (clients.find(clientID) != clients.end() && chatRooms.find(chatID) != chatRooms.end()) {
		chatRooms[chatID].participants.insert(clientID);
		clients[clientID].activeChatIDs.insert(chatID);
		ReleaseMutex(chatMutex);
		ReleaseMutex(clientMutex);
		return true;
	}

	ReleaseMutex(chatMutex);
	ReleaseMutex(clientMutex);
	return false;
}

int GenerateClientID() {
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (clients.find(i) == clients.end()) {
			return i; // Возвращаем первый свободный ID
		}
	}
	return -1; // Нет доступного ID
}

DWORD WINAPI HandleClientCommands(LPVOID lpParam) {
	int clientID = *(int*)lpParam;
	SOCKET clientSocket = clients[clientID].clientSocket;
	std::cout << "Client " << clientID << " connected." << std::endl;
	SendMessageToClient(clientSocket, "Your ID: " + std::to_string(clientID));
	char buffer[BUFFER_SIZE];
	while (true) {
		int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
		if (bytesReceived > 0) {
			std::string command(buffer, bytesReceived);
			try {
				if (command.rfind("/private ", 0) == 0) {
					// Личное сообщение другому клиенту
					size_t spacePos = command.find(' ', 9);
					int targetClientID = std::stoi(command.substr(9, spacePos - 9));
					std::string msg = command.substr(spacePos + 1);
					if (SendPrivateMessage(clientID, targetClientID, msg) != 0) {
						SendMessageToClient(clientSocket, "Client does not exists: " + std::to_string(targetClientID));
					}
					else {
						SendMessageToClient(clientSocket, "Message sent to: " + std::to_string(targetClientID));
					}
				}
				else if (command == "/createChat") {
					// Создание нового чата
					int chatID = CreateChatRoom();
					SendMessageToClient(clientSocket, "Chat created with ID: " + std::to_string(chatID));

				}
				else if (command.rfind("/joinChat ", 0) == 0) {
					// Подключение к чату
					int chatID = std::stoi(command.substr(10));
					if (JoinChatRoom(clientID, chatID)) {
						SendMessageToClient(clientSocket, "Joined chat: " + std::to_string(chatID));
					}
					else {
						SendMessageToClient(clientSocket, "Failed to join chat.");
					}

				}
				else if (command.rfind("/messageChat ", 0) == 0) {
					// Отправка сообщения в чат
					size_t spacePos = command.find(' ', 13);
					int chatID = std::stoi(command.substr(13, spacePos - 13));
					std::string msg = command.substr(spacePos);
					SendMessageToChat(clientID, chatID, "Client " + std::to_string(clientID) + ": " + " FromChat: " + std::to_string(chatID) + ": " + msg);

				}
				else if (command.rfind("/leaveChat ", 0) == 0) {
					// Покинуть чат
					int chatID = std::stoi(command.substr(11));
					int res = LeaveChatRoom(clientID, chatID);
					switch (res) {
					case 0: {
						SendMessageToClient(clientSocket, "Left chat: " + std::to_string(chatID));
						break;
					}
					case 1: {
						SendMessageToClient(clientSocket, "You are not a member of chat " + std::to_string(chatID));
						break;
					}
					case 2: {
						SendMessageToClient(clientSocket, "Chat doesn't exist: " + std::to_string(chatID));
						break;
					}
					default: {
						break;
					}
					}
				}
				else if (command == "/exit") {
					break; // Завершение работы клиента
				}
			}
			catch (...) {
				SendMessageToClient(clientSocket, "Error command: " + command);
			}
		}
		else {
			break; // Завершение работы при ошибке
		}
	}
	DisconnectClient(clientID);
	WaitForSingleObject(clientMutex, INFINITE);
	clients.erase(clientID);
	ReleaseMutex(clientMutex);
	return 0;
}





// Основной серверный цикл
int main() {
	WSADATA wsaData;
	SOCKET serverSocket, clientSocket;
	SOCKADDR_IN serverAddr, clientAddr;

	// Инициализация WinSock
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	// Создание сокета сервера
	serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serverSocket == INVALID_SOCKET) {
		std::cerr << "Ошибка создания сокета!" << std::endl;
		WSACleanup();
		return -1;
	}

	// Параметры сервера
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(PORT);

	// Привязка сокета
	if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		std::cerr << "Ошибка привязки сокета!" << std::endl;
		closesocket(serverSocket);
		WSACleanup();
		return -1;
	}

	// Прослушивание подключений
	if (listen(serverSocket, MAX_CLIENTS) == SOCKET_ERROR) {
		std::cerr << "Ошибка прослушивания на сокете!" << std::endl;
		closesocket(serverSocket);
		WSACleanup();
		return -1;
	}

	std::cout << "Server is waiting on port " << PORT << "..." << std::endl;

	// Создание мьютексов для синхронизации
	clientMutex = CreateMutex(NULL, FALSE, NULL);
	chatMutex = CreateMutex(NULL, FALSE, NULL);

	int clientSize = sizeof(clientAddr);
	while (true) {
		clientSocket = accept(serverSocket, (SOCKADDR*)&clientAddr, &clientSize);
		if (clientSocket == INVALID_SOCKET) {
			std::cerr << "Ошибка подключения клиента!" << std::endl;
			continue;
		}

		int clientID = GenerateClientID();
		if (clientID == -1) {
			std::cerr << "Превышен лимит клиентов!" << std::endl;
			closesocket(clientSocket);
			continue;
		}

		// Сохранение информации о клиенте
		WaitForSingleObject(clientMutex, INFINITE);
		clients[clientID] = { clientSocket, clientID };
		ReleaseMutex(clientMutex);

		// Создание потока для клиента
		HANDLE clientThread = CreateThread(NULL, 0, HandleClientCommands, (LPVOID)&clientID, 0, NULL);
		if (clientThread == NULL) {
			std::cerr << "Ошибка создания потока для клиента!" << std::endl;
			closesocket(clientSocket);
		}
		else {
			CloseHandle(clientThread);
		}
	}

	// Очистка ресурсов
	CloseHandle(clientMutex);
	CloseHandle(chatMutex);
	closesocket(serverSocket);
	WSACleanup();
	return 0;
}
