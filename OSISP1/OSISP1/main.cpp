#include <windows.h>
#include <iostream>
#include <strsafe.h>
#include <unordered_map>
#include <vector>

DWORD WINAPI MyThreadFunction(LPVOID lpParam);

struct MyData {
	std::vector<int> val;
	std::unordered_map<int, int> freqMap;
	size_t size;
	short thread_no;
};



int findMode(const std::unordered_map<int, int>& freqMap) {
	int mode = -1;
	int maxCount = 0;

	for (const auto& entry : freqMap) {
		if (entry.second > maxCount) {
			maxCount = entry.second;
			mode = entry.first;
		}
	}

	return mode;
}

std::unordered_map<int, int> combineFrequencyMaps(MyData** dataArray, int threadCount) {
	std::unordered_map<int, int> combinedFreqMap;

	for (int i = 1; i < threadCount; ++i) {
		for (const auto& entry : (dataArray[i])->freqMap) {
			combinedFreqMap[entry.first] += entry.second;
		}
	}

	return combinedFreqMap;
}

HANDLE output;
HANDLE gConsoleMutex;

double CalculateCPULoad(unsigned long long idleTicks, unsigned long long totalTicks) {
	static unsigned long long prevTotalTicks = 0;
	static unsigned long long prevIdleTicks = 0;

	unsigned long long totalTicksSinceLastTime = totalTicks - prevTotalTicks;
	unsigned long long idleTicksSinceLastTime = idleTicks - prevIdleTicks;

	double cpuLoad = 1.0 - ((totalTicksSinceLastTime > 0) ?
		(double)idleTicksSinceLastTime / totalTicksSinceLastTime : 0);

	prevTotalTicks = totalTicks;
	prevIdleTicks = idleTicks;
	return cpuLoad * 100;
}

unsigned long long FileTimeToInt64(const FILETIME& ft) {
	return (((unsigned long long)(ft.dwHighDateTime)) << 32) | ((unsigned long long)ft.dwLowDateTime);
}

double GetCPULoad() {
	FILETIME idleTime, kernelTime, userTime;
	if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
		return CalculateCPULoad(FileTimeToInt64(idleTime),
			FileTimeToInt64(kernelTime) + FileTimeToInt64(userTime));
	}
	return -1.0;
}


int main()
{
	DWORD threadsNum;
	int size;
	std::cout << "Input num of threads: ";
	std::cin >> threadsNum;
	std::cout << "Input num of elements: ";
	std::cin >> size;
	if (threadsNum <= 0 || size <= 0) {
		std::cout << "Wrong input";
		ExitProcess(1);
	}
	size_t size_for_thread = size / threadsNum;
	int rem = size_for_thread * threadsNum - size;
	double initialCPULoad = GetCPULoad();
	std::cout << "Initial CPU Load: " << initialCPULoad << "%" << std::endl;

	MyData** pDataArray = new MyData * [threadsNum];
	DWORD* dwThreadIdArray = new DWORD[threadsNum];
	HANDLE* hThreadArray = new HANDLE[threadsNum];
	output = GetStdHandle(STD_OUTPUT_HANDLE);
	gConsoleMutex = CreateMutex(NULL, FALSE, NULL);
	if (gConsoleMutex == NULL) {
		ExitProcess(2);
	}
	for (int i = 0; i < threadsNum; i++)
	{
		pDataArray[i] = new MyData;

		if (pDataArray[i] == NULL)
		{
			ExitProcess(3);
		}
		if (i != threadsNum - 1) {
			pDataArray[i]->size = size_for_thread;
		}
		else {
			pDataArray[i]->size = size_for_thread + rem;
		}
		pDataArray[i]->val = std::vector<int>(pDataArray[i]->size);
		pDataArray[i]->thread_no = i + 1;
		for (int j = 0; j < pDataArray[i]->size; ++j) {
			pDataArray[i]->val[j] = rand();
		}

		// Create the thread to begin execution on its own.

		hThreadArray[i] = CreateThread(
			NULL,                   // default security attributes
			0,                      // use default stack size  
			MyThreadFunction,       // thread function name
			pDataArray[i],          // argument to thread function 
			0,                      // use default creation flags 
			&dwThreadIdArray[i]);   // returns the thread identifier 


		// Check the return value for success.
		// If CreateThread fails, terminate execution. 
		// This will automatically clean up threads and memory. 

		if (hThreadArray[i] == NULL)
		{
			ExitProcess(4);
		}
	}
	WaitForMultipleObjects(threadsNum, hThreadArray, TRUE, INFINITE);
	std::unordered_map<int, int> combinedFreqMap = combineFrequencyMaps(pDataArray, threadsNum);

	int mode = findMode(combinedFreqMap);

	SetConsoleCursorPosition(output, { 0,short(threadsNum + 3) });
	std::cout << "Result: " << mode << '\n';

	double finalCPULoad = GetCPULoad();
	std::cout << "Final CPU Load: " << finalCPULoad << "%" << std::endl;

	for (int i = 0; i < threadsNum; i++)
	{
		CloseHandle(hThreadArray[i]);

		if (pDataArray[i] != NULL)
		{
			delete pDataArray[i];
		}
	}
	delete[] pDataArray;
	delete[] dwThreadIdArray;
	delete[] hThreadArray;
	return 0;
}


DWORD WINAPI MyThreadFunction(LPVOID lpParam)
{
	MyData* pDataArray = (MyData*)lpParam;
	double initialCPULoad = GetCPULoad();
	WaitForSingleObject(gConsoleMutex, INFINITE);
	SetConsoleCursorPosition(output, { 0,short(pDataArray->thread_no + 2) });
	std::cout << "Thread " << pDataArray->thread_no << " is pending... CPU load : " << initialCPULoad << " % " << std::endl;
	ReleaseMutex(gConsoleMutex);
	for (int i = 0; i < pDataArray->size; ++i) {
		pDataArray->freqMap[pDataArray->val[i]]++;
	}
	double CPULoad = GetCPULoad();
	WaitForSingleObject(gConsoleMutex, INFINITE);
	SetConsoleCursorPosition(output, { 0,short(pDataArray->thread_no + 2) });
	std::cout << "Thread " << pDataArray->thread_no << " is done. CPU load: " << CPULoad << " %                      " << std::endl;

	ReleaseMutex(gConsoleMutex);
	return 0;
}