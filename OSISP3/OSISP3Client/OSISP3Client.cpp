#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <thread>

const std::wstring SERVER_MUTEX_NAME = L"mutex1";       // Имя мьютекса сервера
const std::wstring CHAT_MUTEX_NAME = L"chatMutex";       // Имя мьютекса сервера
const std::wstring PIPE_NAME_PREFIX = L"\\\\.\\pipe\\ClientPipe_"; // Префикс имен каналов
const std::wstring PIPE_CHAT_NAME_PREFIX = L"\\\\.\\pipe\\ChatPipe_"; // Префикс имен каналов
const std::wstring PIPE_NAME_PREFIX_READ = L"\\\\.\\pipe\\ClientPipeRead_"; // Префикс имен каналов
const std::wstring SERVER_PIPE_NAME = L"\\\\.\\pipe\\ServerPipe"; // Префикс имен каналов

int clientID = 0; // ID текущего клиента

HANDLE pipe;
HANDLE chatPipe;
HANDLE pipeRead;
HANDLE coutMutex;
HANDLE chatMutex;

bool ConnectToChat(int chatID) {
	chatMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, (CHAT_MUTEX_NAME + std::to_wstring(chatID)).c_str());
	std::wstring pipeName = PIPE_CHAT_NAME_PREFIX + std::to_wstring(chatID);
	if (WaitForSingleObject(chatMutex, INFINITE) != WAIT_OBJECT_0) {
		std::cerr << "Failed to acquire server mutex." << std::endl;
		CloseHandle(chatMutex);
		return false;
	}
	// Подключаемся к каналу чата
	chatPipe = CreateFileW(
		pipeName.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0, NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (pipe == INVALID_HANDLE_VALUE) {
		std::cerr << "Failed to connect to chat pipe: " << GetLastError() << std::endl;
		ReleaseMutex(chatMutex);
		return false;
	}

	std::cout << "Connected to chat with ID: " << chatID << std::endl;
	return true;
}

bool ConnectToServer() {
	HANDLE serverMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, SERVER_MUTEX_NAME.c_str());
	if (serverMutex == NULL) {
		std::cerr << "Failed to open server mutex. Error: " << GetLastError() << std::endl;
		return false;
	}

	if (WaitForSingleObject(serverMutex, INFINITE) != WAIT_OBJECT_0) {
		std::cerr << "Failed to acquire server mutex." << std::endl;
		CloseHandle(serverMutex);
		return false;
	}

	// Создаем канал для связи
	HANDLE serverPipe = CreateFileW(
		SERVER_PIPE_NAME.c_str(),               // Имя канала
		GENERIC_READ,                  // Доступ на Чтение
		0,                              // Отсутствие совместного доступа
		NULL,                           // Атрибуты безопасности
		OPEN_EXISTING,                  // Открыть только существующий канал
		FILE_ATTRIBUTE_NORMAL,           // Асинхронный ввод-вывод
		NULL                            // Шаблон файла
	);

	// Получение ID от сервера
	DWORD bytesRead;
	if (!ReadFile(serverPipe, &clientID, sizeof(clientID), &bytesRead, NULL) || bytesRead != sizeof(clientID)) {
		std::cerr << "Failed to read client ID from server." << std::endl;
		CloseHandle(serverPipe);
		ReleaseMutex(serverMutex);
		CloseHandle(serverMutex);
		return false;
	}
	ReleaseMutex(serverMutex);

	std::wstring pipeName = PIPE_NAME_PREFIX + std::to_wstring(clientID);
	std::wstring pipeNameRead = PIPE_NAME_PREFIX + std::to_wstring(clientID);
	pipe = CreateFileW(
		pipeName.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0, NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	pipeRead = CreateFileW(
		pipeNameRead.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0, NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (pipe == INVALID_HANDLE_VALUE) {
		std::cerr << "Failed to connect to pipe: " << GetLastError() << std::endl;
		return false;
	}
	std::cout << "Connected to server with Client ID: " << clientID << std::endl;

	return true;
}

void ListenToServer() {
	WCHAR buffer[1024];
	DWORD bytesRead;

	while (true) {
		BOOL result = ReadFile(pipeRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL);


		if (!result || bytesRead == 0) {
			std::cerr << "Connection closed by server." << std::endl;
			break;
		}
		WaitForSingleObject(coutMutex, INFINITE);
		buffer[bytesRead / sizeof(wchar_t)] = L'\0'; // Добавляем терминальный нуль для корректного вывода строки

		std::wcout << L"\nMessage " << buffer << std::endl << std::flush;
		ReleaseMutex(coutMutex);
	}
}


void SendMessageToServer(const std::wstring& targetID, const std::wstring& message) {
	// Захват мьютекса сервера
	HANDLE serverMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, SERVER_MUTEX_NAME.c_str());
	if (serverMutex == NULL) {
		std::cerr << "Failed to open server mutex. Error: " << GetLastError() << std::endl;
		return;
	}

	if (WaitForSingleObject(serverMutex, INFINITE) != WAIT_OBJECT_0) {
		std::cerr << "Failed to acquire server mutex." << std::endl;
		CloseHandle(serverMutex);
		return;
	}


	if (pipe == INVALID_HANDLE_VALUE) {
		std::cerr << "Failed to connect to pipe: " << GetLastError() << std::endl;
		ReleaseMutex(serverMutex); // Освобождаем мьютекс перед завершением
		CloseHandle(serverMutex);
		return;
	}

	// Форматируем сообщение в формате "To[id]: message"
	std::wstring formattedMessage = L"To" + targetID + L": " + message;
	DWORD bytesWritten;
	DWORD bytesAvailable;
	if (PeekNamedPipe(pipe, NULL, 0, NULL, &bytesAvailable, NULL)) {
		// Можно выполнять запись
		BOOL result = WriteFile(pipe, formattedMessage.c_str(), formattedMessage.size() * sizeof(WCHAR), &bytesWritten, NULL);
		if (!result || bytesWritten != formattedMessage.size() * sizeof(WCHAR)) {
			std::cerr << "Failed to write message to pipe: " << GetLastError() << std::endl;
		}
		else {
			WaitForSingleObject(coutMutex, INFINITE);
			std::cout << std::endl;
			std::wcout << L"Message sent to client " << targetID << ": " << message << std::endl;
			ReleaseMutex(coutMutex);
		}
	}
	else {
		std::cerr << "No client connected or no data available." << std::endl;
	}
	//BOOL result = WriteFile(pipe, formattedMessage.c_str(), formattedMessage.size(), &bytesWritten, NULL);



	ReleaseMutex(serverMutex); // Освобождаем мьютекс после отправки сообщения
	CloseHandle(serverMutex);
}

// Основная функция клиента
int main() {
	coutMutex = CreateMutexW(NULL, FALSE, NULL);
	if (!ConnectToServer()) {
		return 1;
	}
	std::wstring targetID;
	std::wstring message;
	std::thread listenerThread(ListenToServer);
	listenerThread.detach();

	while (true) {
		WaitForSingleObject(coutMutex, INFINITE);
		std::cout << "\nEnter the target client ID (or 'exit' to quit): ";
		ReleaseMutex(coutMutex);
		std::wcin >> targetID;
		if (targetID == L"exit") {
			break;
		}
		WaitForSingleObject(coutMutex, INFINITE);
		std::cout << "\nEnter your message: ";
		ReleaseMutex(coutMutex);
		std::cin.ignore(); // Игнорируем оставшийся символ новой строки
		std::getline(std::wcin, message);

		// Отправляем сообщение
		SendMessageToServer(targetID, message);
	}

	CloseHandle(pipe);
	return 0;
}
