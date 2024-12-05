#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>      // Для _open_osfhandle
#include <fcntl.h>   // Для _O_RDONLY
#include <fstream>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <string>
#include <iostream>
#pragma comment(lib, "Ws2_32.lib")

#define MAX_CLIENTS 10
#define SHARED_MEMORY_SIZE 1024
#define PIPE_BUFFER_SIZE 1024
#define SHARED_CLIENTS 5



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

int HandleToFD(HANDLE handle) {
	return _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY);
}

void SendFile(SOCKET clientSocket, const std::string& fileName) {
	std::ifstream file(fileName, std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Server: Error opening file " << fileName << std::endl;

		// Send a zero size if file doesn't exist
		int64_t zeroSize = 0;
		send(clientSocket, reinterpret_cast<char*>(&zeroSize), sizeof(zeroSize), 0);
		return;
	}

	// Get file size
	file.seekg(0, std::ios::end);
	int64_t fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	// Send file size first
	std::cout << "Server: Sending file size: " << fileSize << " for " << fileName << std::endl;
	send(clientSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0);

	// Send the file content
	char buffer[1024];
	while (file.read(buffer, sizeof(buffer)) || file.gcount()) {
		send(clientSocket, buffer, file.gcount(), 0);
	}
	file.close();
}

void ReceiveFile(SOCKET clientSocket) {
	// Receive file size from the client
	int64_t fileSize = 0;
	int bytesReceived = recv(clientSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0);
	if (bytesReceived <= 0) {
		std::cerr << "Server: Failed to receive file size from client." << std::endl;
		return;
	}

	// Create a new file to save the received data
	std::ofstream outFile("uploaded_file.txt", std::ios::binary);
	if (!outFile.is_open()) {
		std::cerr << "Server: Error opening file to write." << std::endl;
		return;
	}

	// Receive the file content
	char buffer[100024];
	int64_t bytesReceivedTotal = 0;
	while (bytesReceivedTotal < fileSize) {
		int bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

		outFile.write(buffer, bytesRead);
		bytesReceivedTotal += bytesRead;
	}

	outFile.close();
	std::cout << "Server: File received and saved as 'uploaded_file'." << std::endl;
}

int main() {
	WSADATA wsaData;
	//WSAStartup(MAKEWORD(2, 2), &wsaData);
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		std::cerr << "WSAStartup failed with error: " << result << std::endl;
	}

	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET) {
		std::cerr << "Server: Failed to create socket." << std::endl;
		WSACleanup();
		return 1;
	}

	sockaddr_in serverAddr = {};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(9001);
	serverAddr.sin_addr.s_addr = INADDR_ANY;

	if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		std::cerr << "Server: Bind failed." << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	listen(listenSocket, SOMAXCONN);
	std::cout << "Server is waiting for connections\n";
	fd_set master_read_set, read_set, master_write_set, write_set;
	FD_ZERO(&master_read_set);
	FD_ZERO(&master_write_set);

	FD_SET(listenSocket, &master_read_set);

	HANDLE sharedMemoryHandles[SHARED_CLIENTS];
	LPVOID sharedMemoryViews[SHARED_CLIENTS];
	HANDLE eventHandles[SHARED_CLIENTS];
	HANDLE fileMutexes[SHARED_CLIENTS];
	HANDLE fileEvents[SHARED_CLIENTS];
	HANDLE pipeMutexes[SHARED_CLIENTS];
	HANDLE eventMutexes[SHARED_CLIENTS];
	HANDLE pipes[SHARED_CLIENTS];
	OVERLAPPED overlapped[SHARED_CLIENTS];

	for (int i = 0; i < SHARED_CLIENTS; ++i) {

		WCHAR pipeName[256];
		swprintf(pipeName, 256, L"\\\\.\\pipe\\NamedPipe_%d", i + 1);

		pipes[i] = CreateNamedPipeW(
			pipeName,               // Имя канала
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,        // Двусторонняя связь
			PIPE_TYPE_MESSAGE |        // Сообщения как единицы передачи
			PIPE_READMODE_MESSAGE |    // Режим чтения сообщений
			PIPE_WAIT,                 // Синхронный режим
			PIPE_UNLIMITED_INSTANCES,  // Неограниченное количество экземпляров
			PIPE_BUFFER_SIZE,               // Размер выходного буфера
			PIPE_BUFFER_SIZE,               // Размер входного буфера
			0,                         // Таймаут по умолчанию (0 = бесконечное ожидание)
			NULL                       // Атрибуты безопасности
		);
		if (pipes[i] == INVALID_HANDLE_VALUE) {
			std::cerr << "Server: CreateNamedPipe failed with error: " << GetLastError() << std::endl;
		}
		InitializeOverlapped(overlapped[i]);
		if (!ConnectPipeAsync(pipes[i], overlapped[i])) {
			return 1; // Ошибка подключения
		}

		// Создаем отображение памяти
		HANDLE fh = CreateFileW(
			(L"shared" + std::to_wstring(i + 1) + L".txt").c_str(),
			GENERIC_READ | GENERIC_WRITE, // Доступ на чтение и запись
			0,                       // Без общего доступа
			NULL,                    // Стандартный атрибут безопасности
			OPEN_ALWAYS,             // Открыть файл, если существует, или создать новый
			FILE_ATTRIBUTE_NORMAL,   // Обычный файл
			NULL                     // Без шаблона файла
		);

		fileMutexes[i] = CreateMutexW(NULL, FALSE, (L"FileMutex_" + std::to_wstring(i + 1)).c_str());
		pipeMutexes[i] = CreateMutexW(NULL, FALSE, (L"PipeMutex_" + std::to_wstring(i + 1)).c_str());
		eventMutexes[i] = CreateMutexW(NULL, FALSE, (L"EventMutex_" + std::to_wstring(i + 1)).c_str());
		eventHandles[i] = CreateEventW(
			NULL,       // Дефолтные атрибуты безопасности
			FALSE,      // Автоматический сброс
			FALSE,      // Состояние по умолчанию: не установлен
			(L"DataReadyEvent_" + std::to_wstring(i + 1)).c_str() // Имя события (должно совпадать с клиентом)
		);
		fileEvents[i] = CreateEventW(
			NULL,       // Дефолтные атрибуты безопасности
			FALSE,      // Автоматический сброс
			FALSE,      // Состояние по умолчанию: не установлен
			(L"FileReadyEvent_" + std::to_wstring(i + 1)).c_str() // Имя события (должно совпадать с клиентом)
		);


		sharedMemoryHandles[i] = CreateFileMappingW(
			fh,               // Отображение в память без файла
			NULL,                               // Без атрибутов безопасности
			PAGE_READWRITE,                     // Права доступа
			0,                                  // Максимальный размер (старшие 32 бита)
			SHARED_MEMORY_SIZE,                 // Размер отображения памяти
			(L"SharedMemory_" + std::to_wstring(i + 1)).c_str() // Имя отображения
		);



		if (sharedMemoryHandles[i] == NULL) {
			std::cerr << "Server: CreateFileMapping failed with error: " << GetLastError() << std::endl;
			return 1;
		}

		// Отображаем память
		sharedMemoryViews[i] = MapViewOfFile(
			sharedMemoryHandles[i],             // Дескриптор отображения
			FILE_MAP_ALL_ACCESS,                // Права доступа
			0,                                   // Смещение в отображении
			0,                                   // Смещение в файле
			SHARED_MEMORY_SIZE                   // Размер отображения
		);

		if (sharedMemoryViews[i] == NULL) {
			std::cerr << "Server: MapViewOfFile failed with error: " << GetLastError() << std::endl;
			CloseHandle(sharedMemoryHandles[i]);
			return 1;
		}

		CloseHandle(fh);
	}
	bool fileSendStatus[MAX_CLIENTS] = { false };
	std::string fileSended[MAX_CLIENTS];
	std::vector<SOCKET> clientSockets(MAX_CLIENTS, 0);

	while (true) {
		read_set = master_read_set;
		write_set = master_write_set;
		int activity = select(0, &read_set, &write_set, NULL, NULL);
		if (activity == SOCKET_ERROR) {
			std::cerr << "Server: Select error." << std::endl;
			break;
		}

		// Accept new connections
		if (FD_ISSET(listenSocket, &read_set)) {
			SOCKET newClient = accept(listenSocket, NULL, NULL);
			if (newClient != INVALID_SOCKET) {
				bool added = false;
				for (int i = 0; i < MAX_CLIENTS; ++i) {
					if (clientSockets[i] == 0) {
						clientSockets[i] = newClient;
						FD_SET(newClient, &master_read_set); // Add to read set initially to read client requests
						std::cout << "Server: New client connected." << std::endl;
						added = true;
						break;
					}
				}
				if (!added) {
					std::cerr << "Server: Max clients reached." << std::endl;
					closesocket(newClient);
				}
			}
		}

		// Check for data from clients
		for (int i = 0; i < MAX_CLIENTS; ++i) {
			SOCKET clientSock = clientSockets[i];
			if (clientSock == 0) continue;
			char requestedFile[256];
			// If the client is in the read set, check for incoming data
			if (FD_ISSET(clientSock, &read_set)) {

				int bytesReceived = recv(clientSock, requestedFile, sizeof(requestedFile), 0);
				if (bytesReceived > 0) {
					std::string request(requestedFile);

					if (std::filesystem::exists(request)) {
						std::cout << "Server: Client requested file " << request << std::endl;
						fileSendStatus[i] = true;  // Client is ready to receive data
						fileSended[i] = request;
						FD_CLR(clientSock, &master_read_set);  // Stop reading from this client
						FD_SET(clientSock, &master_write_set); // Start monitoring write readiness
					}
					else {
						std::cout << "Server: Client is sending a file." << std::endl;
						ReceiveFile(clientSock);
					}
				}
				else {
					std::cerr << "Server: Client disconnected." << std::endl;
					closesocket(clientSock);
					FD_CLR(clientSock, &master_read_set);
					FD_CLR(clientSock, &master_write_set); // Ensure it's removed from both sets
					clientSockets[i] = 0;
				}
			}

			// If the client is in the write set, send the requested file
			if (fileSendStatus[i] && FD_ISSET(clientSock, &write_set)) {
				std::cout << "Server: Sending file to client." << std::endl;
				SendFile(clientSock, fileSended[i]);  // Example file send

				// After sending, stop monitoring for write and start monitoring for reads again
				fileSendStatus[i] = false;
				FD_CLR(clientSock, &master_write_set);  // Stop writing
				FD_SET(clientSock, &master_read_set);   // Start reading again
			}
		}

		DWORD waitResult;
		do {
			waitResult = WaitForMultipleObjects(
				SHARED_CLIENTS,                // Количество событий
				eventHandles,     // Массив событий
				FALSE,            // Ждем только одно событие
				0                 // Неблокирующий вызов
			);

			if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + 5) {
				// Индекс сработавшего события
				int clientIndex = waitResult - WAIT_OBJECT_0;
				std::cout << "Server: Data ready from client " << clientIndex + 1 << std::endl;

				// Чтение данных из отображенной памяти
				char* data = (char*)sharedMemoryViews[clientIndex];
				std::cout << "Server: Received data from shared memory: " << data << std::endl;
			}
		} while (waitResult != WAIT_TIMEOUT && waitResult != WAIT_FAILED);

		if (waitResult == WAIT_FAILED) {
			std::cerr << "Server: WaitForMultipleObjects failed with error: " << GetLastError() << std::endl;
			break;
		}

		for (int i = 0; i < SHARED_CLIENTS; ++i) {
			CheckPipeForData(pipes[i]);
		}
	}
	for (int i = 0; i < SHARED_CLIENTS; ++i) {
		UnmapViewOfFile(sharedMemoryViews[i]);
		CloseHandle(sharedMemoryHandles[i]);
		CloseHandle(eventHandles[i]);
		CloseHandle(fileMutexes[i]);
		CloseHandle(pipeMutexes[i]);
		CloseHandle(pipes[i]);

	}

	closesocket(listenSocket);
	WSACleanup();
	return 0;
}
