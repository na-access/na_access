// na_access_files.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

using namespace std;

typedef stack<CString> STACK_FILES;

CCriticalSection g_cs;
CCriticalSection g_cs_log;

bool _logging = false;
volatile bool logging = true;
CFile logFile;

volatile bool run = true;
volatile int threads = 0;
volatile long files_size = 0;
volatile long max_files_size = 0;
int max_threads = 10;
volatile long files_found = 0;
volatile long files_opened = 0;
volatile long files_error = 0;
int path_depth = 0;
int client_count = 0;
int client_id = 0;

STACK_FILES files;

void yield() {
	MSG message;
	while (::PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
		::TranslateMessage(&message);
		::DispatchMessage(&message);
	}
}

// add file to array
void addFile(CString file) {
	g_cs.Lock();
	files.push(file);
	files_size++;
	if (max_files_size < files_size)
	{
		max_files_size = files_size;
	}
	g_cs.Unlock();
}
// returns the top file and removes it from array
CString getFile() {
	CString file = CString(files.top());
	files.pop();
	files_size--;
	return file;
}
// return the size of files array
long getSize() {
	return files_size;
}
// deletes log if it exists
void logDelete() {
	if (logging) {
		g_cs_log.Lock();

		WIN32_FIND_DATA FindFileData;
		HANDLE hFind;

		hFind = FindFirstFile(logFile.GetFilePath(), &FindFileData);
		if (hFind != INVALID_HANDLE_VALUE) 
		{
			try
			{
				CFile::Remove(logFile.GetFilePath());
			}
			catch (CFileException* pEx)
			{
				// unable to remove: disable logging
				logging = false;
				pEx->Delete();
			}
		}
		g_cs_log.Unlock();
	}
}
// logs file that wasn't able to be processed
void logFileError(CString filepath) {
	if (logging) {
		g_cs_log.Lock();
		if (logFile.m_hFile == CFile::hFileNull)
		{
			// we'll use a CFileException object to get error information
			CFileException ex;
			// create log file
			if (!logFile.Open(logFile.GetFilePath(), CFile::modeCreate | CFile::modeWrite, &ex))
			{
				// couldn't createlog file, disable logging
				logging = false;
				return;
			}
		}
		// log file error
		CString line = filepath + "\r\n";
		logFile.Write(line, line.GetLength());
		g_cs_log.Unlock();
	}
}
// close log file
void logClose() {
	if (logFile.m_hFile != CFile::hFileNull) {
		g_cs_log.Lock();
		logFile.Flush();
		logFile.Close();
		g_cs_log.Unlock();
	}
}
// open file and read first two bytes.
void openFile(CString filepath) {
	CFile file;

	// we'll use a CFileException object to get error information
	CFileException ex;
	if (file.Open(filepath, CFile::modeRead | CFile::shareDenyNone, &ex))
	{
		if (_logging)
		{
			logFileError(filepath);
		}

		// read 2 bytes and close the file
		BYTE buffer[2];
		file.Read(buffer, 2);
		file.Close();

		// count files
		files_opened++;
	}
	else
	{
		// count files
		files_error++;

		// some error occured, log that file
		logFileError(filepath);

	}
}
// threads method that opens the file
UINT ThreadProc(LPVOID pParam) {
	while (getSize() > 0 || run) {
		CString file;
		g_cs.Lock();
		if (getSize() > 0) {
			file = getFile();
		}
		g_cs.Unlock();
		if (file.GetAllocLength() > 0) {
			openFile(file);
		}
		yield();
	}
	threads--;
	return 0;
}
// add file to array, so the threads process it
// create a thread if max number isn't reached
void processFile(CString file)
{

	files_found++;

	if (max_threads > 0)
	{
		// add file to stack, so the threads do the processing
		addFile(file);
		// create thread if needed
		if (threads < max_threads) {
			threads++;
			CWinThread* thread = AfxBeginThread(ThreadProc, 0, THREAD_PRIORITY_NORMAL);
		}
	}
	else
	{
		// no threads, so open the file
		openFile(file);
	}
}
// processes all elements of path and calls itself recursivly for directories
void processPath(CString path)
{
	CFileFind finder;

	// build a string with wildcards
	CString strWildcard(path);
	strWildcard += _T("\\*.*");

	// start working for files
	BOOL bWorking = finder.FindFile(strWildcard);

	// counter for multi machine mode
	LONGLONG counter = -1;

	// increase path depth
	path_depth++;

	while (bWorking)
	{
		bWorking = finder.FindNextFile();
		// skip . and .. files; otherwise, we'd
		// recur infinitely!
		if (finder.IsDots())
			continue;

		// increase counter
		counter++;

		bool process_it = client_count == 0 || path_depth > 1;
		if (!process_it)
		{
			// multi machine mode is enabled, determine if it's for me
			process_it = counter % client_count == client_id;
		}

		if (process_it)
		{
			CString dir = finder.GetFilePath();
			if (finder.IsDirectory())
			{
				// it's a directory, recursively search it
				processPath(dir);
			}
			else
			{
				// it's a file, process it
				processFile(dir);
			}

		}
	}

	finder.Close();

	// decrease path depth
	path_depth--;
}
int _tmain(int argc, _TCHAR* argv[])
{
	_set_error_mode(_OUT_TO_STDERR);

	cout << "Filer Virusscan Trigger (c) Network Appliance 2005\n";
	
	// set log file name
	logFile.SetFilePath(CString(argv[0]) + ".error.log");

	if (argc < 2) {
		cout << "\n";
		cout << "Access all files of a path by opening them and reading the first two bytes.\n";
		cout << "A log file (" << logFile.GetFilePath() <<") is generated for those files that were unable to access.\n";
		cout << "\n";
		cout << "Missing parameter.\n";
		cout << "Usage: " << argv[0] << " path [threads [machine_count remainder]]\n";
		cout << "threads: How many threads used for accessing files, default is 10, 0=none.\n";
		cout << "\n";
		cout << "Multi machine mode, by default off (available for first path level):\n";
		cout << "In this mode the counter for found path entries is divided by machine_count.\n";
		cout << "The remainder is 0..machine_count - 1 and if it equals for this machine,\n";
		cout << "the file or path is processed.\n";
		return 1;
	}

	// get number of threads used for accessing the files, default is 10
	if (argc > 2)
	{
		max_threads = atoi(argv[2]);
		if (argc > 4)
		{
			client_count = atoi(argv[3]);
			client_id = atoi(argv[4]);
			// adjust log file name
			char id[255];
			_itoa(client_id, id, 10);
			logFile.SetFilePath(CString(argv[0]) + ".error." + id + ".log");
		}
	}

	// delete log file
	logDelete();

	// get start time
	DWORD startTime = GetTickCount();
	
	// start processing this path
	processPath(CString(argv[1]));

	// all files had been found, wait untill all files had been processed
	run = false;
	
	// main loop that waits till all files had been processed and stack is empty
	while (threads > 0)
	{
		yield();
	}

	DWORD endTime = GetTickCount();
	DWORD time = endTime - startTime;
	if (endTime < startTime)
	{
		// oops, during run the timer had been set to 0, so get real time diff
		time = endTime + 0xFFFFFFFF - startTime;
	}

	// print statistic
	cout << files_found << " files found.\n";
	if (max_threads > 0)
	{
		cout << "Thread's array maximum load was " << max_files_size << " files.\n";
	}
	double av = (double)time / files_opened;
	if (files_opened > 0)
	{
		cout << files_opened << " files accessed (average milliseconds per file " << av << ").\n";
	}
	// print error files
	if (files_error > 0)
	{
		cout << files_error << " files unable to access.\n";
	}

	// close log file
	logClose();
	return 0;
}