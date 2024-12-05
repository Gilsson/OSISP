#include <windows.h>
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <time.h>
#include <fstream>  // Для работы с файлами
#include <chrono>
#include <sstream>
#include <queue>

#define MAX_STAGES 4
#define MAX_CHANNELS 4
#define BARRIER_LIMIT 2

std::string getTime(std::chrono::system_clock::time_point last) {
	auto now = std::chrono::system_clock::now();
	return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count());
}

struct Request {
	int requestID;
	int currentStage;
	std::string data;
};

struct Barrier {
	std::queue<Request> requestQueue;
	int waiters;
	int waitnum;
	CRITICAL_SECTION cs;
	HANDLE barrierEvent;
};

struct Stage {
	HANDLE threads[MAX_CHANNELS];
	Barrier barrier;
	int stageIndex;
	int threadCount;
	int isBusy[MAX_CHANNELS];
	std::chrono::system_clock::time_point point = std::chrono::system_clock::now();
};

std::string ProcessStage1(const std::string& data) {
	std::string result = data;
	std::transform(result.begin(), result.end(), result.begin(), ::toupper);
	return result;
}

std::string ProcessStage2(const std::string& data) {
	std::string result = data;
	std::reverse(result.begin(), result.end());
	return result;
}

std::string ProcessStage3(const std::string& data) {
	std::string result = data;
	std::replace(result.begin(), result.end(), ' ', '_');
	return result;
}

std::string ProcessStage4(const std::string& data) {
	std::string result = data;
	std::reverse(result.begin(), result.end());
	return result;
}

std::string ProcessRequest(int stageIndex, const std::string& data) {
	switch (stageIndex) {
	case 0: return ProcessStage1(data);
	case 1: return ProcessStage2(data);
	case 2: return ProcessStage3(data);
	case 3: return ProcessStage4(data);
	default: return data;
	}
}

Stage stages[MAX_STAGES];
HANDLE eventExit;
CRITICAL_SECTION csLog;

std::ofstream logFile("time.txt", std::ios::out);
std::ofstream logFile2("time2.txt", std::ios::out);

void InitializeBarrier(Barrier* barrier, int waitnum) {
	barrier->waiters = 0;
	barrier->waitnum = waitnum;
	barrier->barrierEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	InitializeCriticalSection(&barrier->cs);
}

void WaitForBarrier(Barrier* barrier) {
	DWORD res = WaitForSingleObject(eventExit, 0);
	if (res != WAIT_OBJECT_0) {
		EnterCriticalSection(&barrier->cs);
		barrier->waiters++;  // Увеличиваем счетчик ожидающих потоков

		if (barrier->waiters < barrier->waitnum) {
			LeaveCriticalSection(&barrier->cs);

			while (WaitForSingleObject(barrier->barrierEvent, 0) != WAIT_OBJECT_0 && WaitForSingleObject(eventExit, 0) != 0) { Sleep(50); }

		}
		else {
			// Все потоки достигли барьера, разрешаем всем продолжить
			SetEvent(barrier->barrierEvent);
			barrier->waiters = 0;  // Сбрасываем счетчик
			ResetEvent(barrier->barrierEvent);  // Сбрасываем событие для следующего этапа
			LeaveCriticalSection(&barrier->cs);
		}
	}
	else { SetEvent(barrier->barrierEvent); }
}

DWORD WINAPI StageThread(LPVOID param) {
	Stage* stage = (Stage*)param;
	int stageIndex = stage->stageIndex;

	while (true) {
		Request request;
		bool hasRequest = false;
		int channelID = -1;
		WaitForBarrier(&stage->barrier);

		EnterCriticalSection(&stage->barrier.cs);
		if (!stage->barrier.requestQueue.empty()) {
			request = stage->barrier.requestQueue.front();
			stage->barrier.requestQueue.pop();
			hasRequest = true;
		}
		LeaveCriticalSection(&stage->barrier.cs);

		if (hasRequest) {
			// Найти доступный канал
			for (int i = 0; i < stage->threadCount; i++) {
				if (stage->isBusy[i] == 0) {
					EnterCriticalSection(&stage->barrier.cs);
					channelID = i;
					stage->isBusy[i] = 1;
					LeaveCriticalSection(&stage->barrier.cs);
					break;
				}
			}

			if (channelID != -1) {
				std::string startTime = getTime(stage->point);
				if (stage->stageIndex % 2 == 0) Sleep(400);
				else Sleep(200);
				request.data = ProcessRequest(stageIndex, request.data);
				std::string endTime = getTime(stage->point);

				EnterCriticalSection(&csLog);
				std::cout << "Request " << request.requestID << " - Stage " << stageIndex + 1
					<< ": Start " << startTime << ", End " << endTime << ", Data: " << request.data << std::endl;
				logFile << request.requestID << " " << stageIndex + 1 << " " << startTime << " " << endTime << std::endl;
				logFile2 << request.requestID << " " << stageIndex + 1 << " " << startTime << " " << endTime << " " << request.data << std::endl;
				stage->isBusy[channelID] = 0;
				LeaveCriticalSection(&csLog);

				if (request.currentStage < MAX_STAGES - 1) {
					++request.currentStage;
					EnterCriticalSection(&stages[request.currentStage].barrier.cs);
					stages[request.currentStage].barrier.requestQueue.push(request);
					LeaveCriticalSection(&stages[request.currentStage].barrier.cs);
				}
			}
		}
		else {
			bool allQueuesEmpty = true;
			for (int i = 0; i <= stageIndex; ++i) {
				EnterCriticalSection(&stages[i].barrier.cs);
				if (!stages[i].barrier.requestQueue.empty()) {
					allQueuesEmpty = false;
					LeaveCriticalSection(&stages[i].barrier.cs);
				}
				else {
					for (int j = 0; j < stages[i].threadCount; ++j) {
						if (stages[i].isBusy[j] != 0) {
							allQueuesEmpty = false;
							break;
						}
					}
					LeaveCriticalSection(&stages[i].barrier.cs);
				}
			}
			if (allQueuesEmpty && WaitForSingleObject(eventExit, 0) == WAIT_OBJECT_0) {
				break;
			}
		}
	}
	return 0;
}



void InitializeStages(Stage stages[], int stageCounts[], int barrierCounts[]) {
	for (int i = 0; i < MAX_STAGES; i++) {
		stages[i].stageIndex = i;
		stages[i].threadCount = stageCounts[i];
		InitializeBarrier(&stages[i].barrier, barrierCounts[i]);

		for (int j = 0; j < stageCounts[i]; j++) {
			stages[i].isBusy[j] = 0;
			stages[i].threads[j] = CreateThread(NULL, 0, StageThread, &stages[i], 0, NULL);
		}
	}
}

DWORD WINAPI RequestGenerator(LPVOID param) {
	int requestID = 0;
	DWORD res = WaitForSingleObject(eventExit, 0);

	while (res != WAIT_OBJECT_0) {
		Request request = { ++requestID, 0, "This is request number #" + std::to_string(requestID) };

		EnterCriticalSection(&stages[0].barrier.cs);
		stages[0].barrier.requestQueue.push(request);
		LeaveCriticalSection(&stages[0].barrier.cs);

		Sleep(50);
		res = WaitForSingleObject(eventExit, 0);
	}
	return 0;
}

void TerminateStages(Stage stages[]) {
	SetEvent(eventExit);
	for (int i = 0; i < MAX_STAGES; i++) {
		for (int j = 0; j < stages[i].threadCount; j++) {
			WaitForSingleObject(stages[i].threads[j], INFINITE);
			CloseHandle(stages[i].threads[j]);
		}
		CloseHandle(&stages[i].barrier.cs);
		CloseHandle(stages[i].barrier.barrierEvent);
	}
}

int main() {

	int stageCounts[MAX_STAGES] = { 4, 4, 2, 2 };
	int barrierCounts[MAX_STAGES] = { 2, 2, 2, 2 };
	InitializeStages(stages, stageCounts, barrierCounts);
	eventExit = CreateEvent(NULL, TRUE, FALSE, NULL);
	InitializeCriticalSection(&csLog);


	HANDLE requestGenerator = CreateThread(NULL, 0, RequestGenerator, NULL, 0, NULL);

	Sleep(1000);

	TerminateStages(stages);
	WaitForSingleObject(requestGenerator, INFINITE);

	DeleteCriticalSection(&csLog);
	CloseHandle(eventExit);
	logFile.close();
	logFile2.close();

	std::cout << "Processing system terminated." << std::endl;
	return 0;
}
