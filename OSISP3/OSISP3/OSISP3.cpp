#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

const int MAX_CLIENTS = 5; // Максимальное количество клиентов
const std::wstring PIPE_NAME = L"\\\\.\\pipe\\ClientPipe_"; // Общий префикс для имен каналов
const std::wstring PIPE_NAME_READ = L"\\\\.\\pipe\\ClientPipeRead_"; // Общий префикс для имен каналов
const std::wstring ID_PIPE_NAME = L"\\\\.\\pipe\\ServerPipe";
std::unordered_map<int, HANDLE> clientMutexes;
HANDLE serverMutex;

void InitializeOverlapped(OVERLAPPED& ol) {
	ZeroMemory(&ol, sizeof(OVERLAPPED));
	ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // Event to signal when the operation completes
	if (ol.hEvent == NULL) {
		std::cerr << "Error creating event: " << GetLastError() << std::endl;
	}
}

bool ConnectPipeAsync(HANDLE pipe, OVERLAPPED& overlapped) {
	BOOL result = ConnectNamedPipe(pipe, &overlapped);

	if (!result) {
		DWORD error = GetLastError();
		if (error == ERROR_IO_PENDING) {
			// Операция асинхронная, продолжаем выполнение
			return true;
		}
		else if (error == ERROR_PIPE_CONNECTED) {
			// Клиент уже подключен
			SetEvent(overlapped.hEvent);
			return true;
		}
		else {
			std::cerr << "Server: ConnectNamedPipe failed with error: " << error << std::endl;
			return false;
		}
	}

	return true; // Успешное подключение
}

// Структура для хранения информации о подключенном клиенте
struct ClientInfo {
	HANDLE pipe;
	HANDLE pipeRead;
	int clientID;
};

// Массив для хранения всех клиентов
std::vector<ClientInfo> clients;


int ExtractClientID(const std::wstring& message) {
	if (message.substr(0, 2) == L"To") {
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



void SendMessageToClient(int clientID, int fromClientID, const std::wstring& message) {
	auto it = std::find_if(clients.begin(), clients.end(), [clientID](const ClientInfo& client) {
		return client.clientID == clientID;
		});
	if (it != clients.end()) {
		HANDLE clientMutex = clientMutexes[clientID];
		WaitForSingleObject(clientMutex, INFINITE); // Захватываем мьютекс клиента
		DWORD bytesWritten;
		BOOL isClientConnected = FALSE;
		DWORD bytesAvailable = 0;
		std::wstring sendMessage = L"From" + std::to_wstring(fromClientID) + L": " + message.substr(5);

		isClientConnected = PeekNamedPipe(it->pipeRead, NULL, 0, NULL, &bytesAvailable, NULL);
		if (isClientConnected) {
			// Клиент подключен - отправляем сообщение
			BOOL result = WriteFile(it->pipeRead, sendMessage.c_str(), sendMessage.length() * 2, &bytesWritten, NULL);
			if (!result) {
				std::cerr << "Error writing to client " << clientID << ", client may have disconnected." << std::endl;
			}
		}
		else {
			std::cerr << "Client " << clientID << " has disconnected." << std::endl;
			// Закрываем ресурсы для клиента, если он отключился
			auto it2 = std::find_if(clients.begin(), clients.end(), [fromClientID](const ClientInfo& client) {
				return client.clientID == fromClientID;
				});
			HANDLE fromClientMutex = clientMutexes[fromClientID];
			WaitForSingleObject(fromClientMutex, INFINITE);
			isClientConnected = PeekNamedPipe(it2->pipeRead, NULL, 0, NULL, &bytesAvailable, NULL);
			if (isClientConnected) {
				// Клиент подключен - отправляем сообщение
				sendMessage = L"Server: Client you want to write is disconnected.";
				BOOL result = WriteFile(it2->pipeRead, sendMessage.c_str(), sendMessage.length() * sizeof(wchar_t), &bytesWritten, NULL);
				if (!result) {
					std::cerr << "Error writing to client " << clientID << ", client may have disconnected." << std::endl;
				}
			}
			ReleaseMutex(fromClientMutex);
		}
		ReleaseMutex(clientMutex); // Освобождаем мьютекс клиента
	}
	else {
		std::cerr << "\nClient with ID " << clientID << " not found." << std::endl;
		auto it2 = std::find_if(clients.begin(), clients.end(), [fromClientID](const ClientInfo& client) {
			return client.clientID == fromClientID;
			});
		HANDLE fromClientMutex = clientMutexes[fromClientID];
		WaitForSingleObject(fromClientMutex, INFINITE);
		DWORD bytesWritten, bytesAvailable;

		BOOL isClientConnected = PeekNamedPipe(it2->pipeRead, NULL, 0, NULL, &bytesAvailable, NULL);
		if (isClientConnected && bytesAvailable == 0) {
			// Клиент подключен - отправляем сообщение
			std::wstring sendMessage = L"Server: Client you want to write isn't connected.";
			BOOL result = WriteFile(it2->pipeRead, sendMessage.c_str(), sendMessage.size() * sizeof(wchar_t), &bytesWritten, NULL);
			if (!result) {
				std::cerr << "Error writing to client " << clientID << ", client may have disconnected." << std::endl;
			}
		}
		ReleaseMutex(fromClientMutex);
	}
}

HANDLE AssignClientID(HANDLE idPipe) {
	static int currentID = 0;

	// Проверка, доступен ли ID

	int assignedID = currentID++;

	std::wstring pipeName = PIPE_NAME + std::to_wstring(assignedID);
	std::wstring pipeNameRead = PIPE_NAME + std::to_wstring(assignedID);
	HANDLE pipe = CreateNamedPipeW(
		pipeName.c_str(),               // Имя канала
		PIPE_ACCESS_DUPLEX,        // Двусторонняя связь
		PIPE_TYPE_MESSAGE |        // Сообщения как единицы передачи
		PIPE_READMODE_MESSAGE |    // Режим чтения сообщений
		PIPE_WAIT,                 // Синхронный режим
		PIPE_UNLIMITED_INSTANCES,  // Неограниченное количество экземпляров
		1024,               // Размер выходного буфера
		1024,               // Размер входного буфера
		0,                         // Таймаут по умолчанию (0 = бесконечное ожидание)
		NULL                       // Атрибуты безопасности
	);

	HANDLE pipeRead = CreateNamedPipeW(
		pipeNameRead.c_str(),               // Имя канала
		PIPE_ACCESS_DUPLEX,        // Двусторонняя связь
		PIPE_TYPE_MESSAGE |        // Сообщения как единицы передачи
		PIPE_READMODE_MESSAGE |    // Режим чтения сообщений
		PIPE_WAIT,                 // Синхронный режим
		PIPE_UNLIMITED_INSTANCES,  // Неограниченное количество экземпляров
		1024,               // Размер выходного буфера
		1024,               // Размер входного буфера
		0,                         // Таймаут по умолчанию (0 = бесконечное ожидание)
		NULL                       // Атрибуты безопасности
	);

	if (pipe == INVALID_HANDLE_VALUE) {
		std::cerr << "Failed to create pipe " << currentID << std::endl;
		return pipe;
	}
	DWORD bytesWritten;
	if (!WriteFile(idPipe, &assignedID, sizeof(assignedID), &bytesWritten, NULL) || bytesWritten != sizeof(assignedID)) {
		std::cerr << "Failed to send client ID." << std::endl;
	}
	else {
		std::cout << "Assigned Client ID: " << assignedID << std::endl;
		// Добавляем клиента в массив для дальнейшего общения
		clients.push_back({ pipe, pipeRead, assignedID });
	}
	//
	// Закрываем pipe после передачи ID
	WaitForSingleObject(serverMutex, INFINITE);
	DisconnectNamedPipe(idPipe);
	ReleaseMutex(serverMutex);
	return pipe;
}

// Функция для обработки общения с клиентом
void HandleClient(ClientInfo clientInfo) {
	WCHAR buffer[1024];
	DWORD bytesRead;

	while (true) {
		// Чтение данных от клиента
		BOOL result = ReadFile(clientInfo.pipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
		if (!result || bytesRead == 0) {
			std::cerr << "Client " << clientInfo.clientID << " disconnected." << std::endl;
			break;
		}

		buffer[bytesRead / sizeof(wchar_t)] = '\0'; // Добавляем терминальный ноль для строкового вывода
		std::wstring message(buffer);

		// Извлекаем ID клиента для отправки
		int targetClientID = ExtractClientID(message);
		if (targetClientID != -1) {
			std::wcout << "Sending message from client " << clientInfo.clientID << " to client " << targetClientID << ": " << message << std::endl;
			SendMessageToClient(targetClientID, clientInfo.clientID, message);
		}
		else {
			std::wcerr << "Invalid target client ID in message: " << message << std::endl;
		}
	}

	CloseHandle(clientInfo.pipe);
}



void CheckPipeForData(HANDLE& pipeHandle) {
	DWORD bytesAvailable = 0;
	DWORD bytesLeftThisMessage = 0;
	DWORD bytesTotal = 0;
	OVERLAPPED op = {};
	InitializeOverlapped(op);
	// Проверяем, есть ли данные в канале
	BOOL result = PeekNamedPipe(
		pipeHandle,          // Дескриптор канала
		NULL,                // Буфер не требуется
		0,                   // Размер буфера (не нужен, так как данные не читаем)
		NULL,                // Число фактически прочитанных байтов
		&bytesAvailable,     // Количество байтов, доступных для чтения
		&bytesLeftThisMessage // Количество байтов в текущем сообщении (для сообщений)
	);


	// Если есть данные в канале
	if (bytesAvailable > 0) {
		std::cout << "Server: Data available in pipe. Bytes to read: " << bytesAvailable << std::endl;

		// Выделяем буфер для чтения данных
		char* buffer = new char[bytesAvailable];
		DWORD bytesRead = 0;

		// Читаем данные из канала
		if (ReadFile(pipeHandle, buffer, bytesAvailable, &bytesRead, &op)) {
			if (GetLastError() == ERROR_IO_PENDING) {
				WaitForSingleObject(op.hEvent, INFINITE);
				GetOverlappedResult(pipeHandle, &op, &bytesRead, TRUE);
			}
			std::cout << "Server: Read data from pipe: " << std::string(buffer, bytesRead) << std::endl;
		}
		else {
			std::cerr << "Server: ReadFile failed with error: " << GetLastError() << std::endl;
		}

		delete[] buffer; // Освобождаем буфер
		if (!DisconnectNamedPipe(pipeHandle)) {
			std::cerr << "Server: Failed to disconnect pipe. Error: " << GetLastError() << std::endl;
		}
		ConnectPipeAsync(pipeHandle, op);
	}
}

int GenerateClientID() {
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		bool isIDTaken = false;
		for (const auto& client : clients) {
			if (client.clientID == i) {
				isIDTaken = true;
				break;
			}
		}
		if (!isIDTaken) {
			return i; // Возвращаем первый свободный ID
		}
	}
	return -1; // Нет свободного ID
}


// Основной серверный цикл
int main() {
	serverMutex = CreateMutexW(NULL, FALSE, L"mutex1");

	for (int i = 0; i < MAX_CLIENTS; ++i) {
		HANDLE clientMutex = CreateMutexW(NULL, FALSE, NULL);
		if (clientMutex == NULL) {
			std::cerr << "Failed to create mutex for client " << i << "." << std::endl;
			return 1;
		}
		clientMutexes[i] = clientMutex;
	}
	HANDLE idPipe = CreateNamedPipeW(
		ID_PIPE_NAME.c_str(),
		PIPE_ACCESS_OUTBOUND,       // Только для записи
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		PIPE_UNLIMITED_INSTANCES,
		512, 512,
		0,
		NULL
	);
	if (idPipe == INVALID_HANDLE_VALUE) {
		std::cerr << "Failed to create ID pipe." << std::endl;
		return 1;
	}

	std::vector<std::thread> clientThreads;
	HANDLE pipes[MAX_CLIENTS];
	HANDLE overlappedEvents[MAX_CLIENTS];
	OVERLAPPED overlapped[MAX_CLIENTS];
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		// Создаем именованный канал

		std::cout << "Waiting for client to connect on pipe " << i << "..." << std::endl;
		if (ConnectNamedPipe(idPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
			// Назначаем клиенту ID

			pipes[i] = AssignClientID(idPipe);


			BOOL result = ConnectNamedPipe(pipes[i], NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
			if (result) {
				int clientID = clients.back().clientID;
				DWORD bytesWritten;
				/*WriteFile(pipes[i], &clientID, sizeof(clientID), &bytesWritten, NULL);*/
				std::cout << "Assigned ID " << clients.back().clientID << " to new client." << std::endl;
				ClientInfo info = { pipes[i], clients.back().pipeRead, clientID };
				// Запуск потока для обработки клиента
				clientThreads.emplace_back(HandleClient, info);
				std::cout << "Client connected on pipe " << i << std::endl;
			}
		}
	}
	for (auto& thread : clientThreads) {
		thread.join();
	}

	for (auto& mutex : clientMutexes) {
		CloseHandle(mutex.second);
	}
	return 0;
}
