#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

#define SHARED_MEMORY_SIZE 1024

void RequestFileShared(const std::wstring& mappingName, const std::wstring& eventName, const std::string& fileName, const std::wstring& mutexName) {

	HANDLE sharedMemoryHandle = OpenFileMapping(
		FILE_MAP_ALL_ACCESS,    // Доступ для чтения и записи
		FALSE, mappingName.c_str());
	if (sharedMemoryHandle == NULL) {
		std::cerr << "Client: CreateFileMapping failed with error: " << GetLastError() << std::endl;
		return;
	}

	LPVOID sharedMemoryView = MapViewOfFile(sharedMemoryHandle, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEMORY_SIZE);
	if (sharedMemoryView == NULL) {
		std::cerr << "Client: MapViewOfFile failed with error: " << GetLastError() << std::endl;
		CloseHandle(sharedMemoryHandle);
		return;
	}

	HANDLE mutex = OpenMutex(SYNCHRONIZE, FALSE, mutexName.c_str());
	if (mutex == NULL) {
		std::cerr << "Client: OpenMutex failed with error: " << GetLastError() << std::endl;
		return;
	}
	HANDLE eventHandle = CreateEvent(NULL, FALSE, FALSE, eventName.c_str());
	if (eventHandle == NULL) {
		std::cerr << "Client: CreateEvent failed with error: " << GetLastError() << std::endl;
		CloseHandle(sharedMemoryHandle);
		return;
	}


	DWORD waitResult = WaitForSingleObject(mutex, INFINITE);
	if (waitResult == WAIT_OBJECT_0) {
		char* buffer = (char*)calloc(SHARED_MEMORY_SIZE, sizeof(char));
		if (buffer != NULL) {
			memset(buffer, fileName[6], SHARED_MEMORY_SIZE);
			memcpy(sharedMemoryView, buffer, SHARED_MEMORY_SIZE);
		}
		SetEvent(eventHandle);
		ReleaseMutex(mutex);

	}
	std::cout << "Client write to shared";
	Sleep(2000);
	UnmapViewOfFile(sharedMemoryView);
	CloseHandle(sharedMemoryHandle);
	CloseHandle(eventHandle);
}

void RequestFileThroughPipe(const std::wstring& pipeName, int pipeNum, const std::wstring& pipeMutexName) {
	HANDLE pipeHandle;
	OVERLAPPED overlapped = { 0 };
	HANDLE eventHandle = CreateEvent(NULL, TRUE, FALSE, NULL);
	// Пытаемся подключиться к каналу с ожиданием
	while (1) {
		std::cout << "Enter loop\n";
		pipeHandle = CreateFile(
			pipeName.c_str(),             // Имя канала
			GENERIC_WRITE,                // Открываем канал только для записи
			0,                            // Не делимся доступом
			NULL,                         // Атрибуты безопасности по умолчанию
			OPEN_EXISTING,                // Открываем существующий канал
			FILE_FLAG_OVERLAPPED,                            // Атрибуты по умолчанию
			NULL                          // Без шаблонного дескриптора
		);

		// Если CreateFile не смог открыть канал, пробуем снова через некоторое время
		if (pipeHandle == INVALID_HANDLE_VALUE) {
			if (GetLastError() == ERROR_PIPE_BUSY) {
				// Если канал занят, ждем, пока он освободится (5 секунд ожидания)
				if (!WaitNamedPipe(pipeName.c_str(), INFINITE)) {
					std::cerr << "Client: Could not open pipe. 5 second wait timed out." << std::endl;
					return;
				}
			}
			else {
				std::cerr << "Client: Failed to open pipe. Error: " << GetLastError() << std::endl;
				return;
			}
		}
		else {
			break;
		}
	}
	overlapped.hEvent = eventHandle;

	// Запись данных в канал
	char buffer[1024];
	memset(buffer, pipeNum + '0', sizeof(buffer)); // Пример данных, создаем массив из символов
	DWORD bytesWritten;

	if (!WriteFile(pipeHandle, buffer, sizeof(buffer), &bytesWritten, &overlapped)) {
		if (GetLastError() == ERROR_IO_PENDING) {
			std::cout << "Client: Writing data asynchronously..." << std::endl;
			WaitForSingleObject(overlapped.hEvent, INFINITE);
			GetOverlappedResult(pipeHandle, &overlapped, &bytesWritten, TRUE);
			std::cout << "Client: Successfully wrote to pipe. Bytes written: " << bytesWritten << std::endl;
		}
		else {
			std::cerr << "Client: WriteFile failed with error: " << GetLastError() << std::endl;
		}
	}
	else {
		std::cout << "Client: Successfully wrote to pipe. Bytes written: " << bytesWritten << std::endl;
	}

	CloseHandle(pipeHandle);
	CloseHandle(eventHandle);


	Sleep(500);  // Небольшая пауза, чтобы дать другим клиентам время на подключение
}

void RequestFile(SOCKET s, const std::string& fileName) {
	// Send the requested file name to the server
	std::cout << "Client: Requesting file " << fileName << std::endl;
	send(s, fileName.c_str(), fileName.size() + 1, 0); // +1 to send null terminator
	// Receive the file size from the server
	int64_t fileSize = 0;
	int bytesReceived = recv(s, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0);
	if (bytesReceived <= 0) {
		std::cerr << "Client: Failed to receive file size from server." << std::endl;
		return;
	}
	std::cout << "Client: File size received: " << fileSize << std::endl;

	// Receive the file content
	std::ofstream outFile("received_" + fileName, std::ios::binary);
	if (!outFile.is_open()) {
		std::cerr << "Client: Error opening file to write." << std::endl;
		return;
	}

	char buffer[1024];
	int64_t bytesReceivedTotal = 0;
	while (bytesReceivedTotal < fileSize) {
		int bytesRead = recv(s, buffer, sizeof(buffer), 0);
		if (bytesRead <= 0) {
			std::cerr << "Client: Error receiving file data." << std::endl;
			break;
		}
		outFile.write(buffer, bytesRead);
		bytesReceivedTotal += bytesRead;
	}

	outFile.close();
}

void SendFile(SOCKET s, const std::string& fileName) {
	send(s, fileName.c_str(), fileName.size() + 1, 0);
	std::ifstream file(fileName, std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Client: Error opening file " << fileName << std::endl;
		return;
	}

	// Get file size
	file.seekg(0, std::ios::end);
	int64_t fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	// Send file size first
	std::cout << "Client: Sending file size: " << fileSize << " for " << fileName << std::endl;
	send(s, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0);

	// Send the file content
	char buffer[100024];
	while (file.read(buffer, sizeof(buffer)) || file.gcount()) {
		send(s, buffer, file.gcount(), 0);
	}

	file.close();
}

int main() {
	srand(static_cast<unsigned int>(time(nullptr))); // Seed for random choice
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		std::cerr << "WSAStartup failed with error: " << result << std::endl;
	}

	SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (clientSocket == INVALID_SOCKET) {
		std::cerr << "Client: Failed to create socket." << std::endl;
		WSACleanup();
		return 1;
	}

	sockaddr_in serverAddr = {};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(9001);
	inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

	// Connect to server
	if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		std::cerr << "Client: Failed to connect to server." << std::endl;
		closesocket(clientSocket);
		WSACleanup();
		return 1;
	}

	// Randomly decide to send or request a file
	srand(time(NULL));
	std::vector<std::string> sendFiles = { "send1.txt", "send2.txt", "send3.txt" };
	std::vector<std::string> requestFiles = { "file1.txt", "file2.txt", "file3.txt" };
	std::vector<std::string> sharedFiles = { "shared1.txt", "shared2.txt","shared3.txt" ,"shared4.txt" ,"shared5.txt" };
	std::vector<std::wstring> mutexNames = { L"FileMutex_1", L"FileMutex_2", L"FileMutex_3", L"FileMutex_4", L"FileMutex_5" };
	std::vector<std::wstring> pipeMutexNames = { L"PipeMutex_1", L"PipeMutex_2", L"PipeMutex_3", L"PipeMutex_4", L"PipeMutex_5" };
	std::vector<std::wstring> eventMutexNames = { L"EventMutex_1", L"EventMutex_2", L"EventMutex_3", L"EventMutex_4", L"EventMutex_5" };

	std::vector<std::wstring> eventNames = { L"DataReadyEvent_1", L"DataReadyEvent_2", L"DataReadyEvent_3", L"DataReadyEvent_4", L"DataReadyEvent_5" };
	std::vector<std::wstring> fileEventNames = { L"FileReadyEvent_1", L"FileReadyEvent_2", L"FileReadyEvent_3", L"FileReadyEvent_4", L"FileReadyEvent_5" };
	std::vector<std::wstring> mappingNames = { L"SharedMemory_1", L"SharedMemory_2", L"SharedMemory_3", L"SharedMemory_4", L"SharedMemory_5" };
	int action = rand() % 3;
	std::string requestedFile;
	std::string fileToSend;
	std::string sharedFile;
	switch (action) {
	case 0:
	{
		// Request a file from socket
		std::cout << "Client: Enter the name of the file to send";
		int index = rand() % 3;
		fileToSend = sendFiles[index];
		std::cout << "\nRandomly choosed file " << fileToSend << std::endl;
		SendFile(clientSocket, fileToSend);
		break;
	}
	case 1:
	{
		// Get file from shared memory
		std::cout << "Client: Enter the name of the file to send as shared";
		int index = rand() % 5;
		sharedFile = sharedFiles[index];
		std::cout << "\nRandomly choosed file " << sharedFile << std::endl;
		RequestFileShared(mappingNames[index], eventNames[index], sharedFile, mutexNames[index]);
		break;
	}
	case 2:
	{
		std::cout << "Client is choosing pipe to use\n";
		int index = rand() % 5;
		std::cout << "Randomly choosed pipe " << index << std::endl;
		RequestFileThroughPipe(L"\\\\.\\pipe\\NamedPipe_" + std::to_wstring(index + 1), index + 1, pipeMutexNames[index]);
		break;
	}
	case 3:
	{
		std::cout << "Client: Enter the name of the file to request";
		int index = rand() % 3;
		requestedFile = requestFiles[index];
		std::cout << "\nRandomly choosed file " << requestedFile << std::endl;
		RequestFile(clientSocket, requestedFile);
	}
	}

	closesocket(clientSocket);
	WSACleanup();
	system("pause");
	return 0;
}
