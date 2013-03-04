#define NO_MAKEFILE
#define UNICODE

#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>

#include <windows.h>
#include <process.h>

#include "na_access_files_private.h"

using namespace std;

HANDLE hSemaphoreLog;
HANDLE hSemaphoreFile;
HANDLE hProcessNextEvent;

bool _logging = false; 			// enables the logging for each file found
bool _read_file = false;		// if true first two bytes of file are read
volatile bool logging = true;
wstring sLogFile;
HANDLE hLogFile = NULL;

volatile bool run = true;

HANDLE* threads;
volatile int running_threads = 0;
volatile int running_threads_same_time = 0;
volatile int running_threads_same_time_max = 0;

const wchar_t* file_threads = NULL;
wchar_t* ext_list = NULL;
int max_threads = 10;

volatile long files_found = 0;
volatile long files_opened = 0;
volatile long files_error = 0;

int path_depth = 0;

int client_count = 0;
int client_id = 0;

void tolower(wchar_t *s) {
	int l = wcslen(s);
	for (int i = 0; i < l; i++) {
		s[i] = tolower(s[i]);
	}
}

// deletes log if it exists
void logDelete() {
	if (logging) {
		WIN32_FIND_DATA FindFileData;
		HANDLE hFind;

		hFind = FindFirstFile(sLogFile.c_str(), &FindFileData);
		if (hFind != INVALID_HANDLE_VALUE)
		{
		   if (!DeleteFile(sLogFile.c_str()))
		   {
			// unable to remove: disable logging
			cout << "Cannot delete log file '" << sLogFile.c_str() << "'!";
			logging = false;
			}
		}
	}
}
// logs file that wasn't able to be processed
void logError(const wchar_t *_w_filepath, const wchar_t *_w_text, int err_no) {
	if (logging) {
		WaitForSingleObject(hSemaphoreLog, INFINITE);
		
		if (hLogFile == NULL)
		{
			// create log file
			hLogFile = CreateFile(sLogFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
			if (hLogFile == INVALID_HANDLE_VALUE)
			{
				// couldn't createlog file, disable logging
				hLogFile = NULL;
				logging = false;
				ReleaseSemaphore(hSemaphoreLog, 1, NULL);
				cout << "Cannot create log file, logging is disabled!\n";
				return;
			}
		}
		// convert wide char to char
		char filepath[wcslen(_w_filepath) + 1];
		char text[wcslen(_w_text) + 1];
		WideCharToMultiByte( CP_ACP, 0, _w_filepath, -1, filepath, sizeof(filepath), NULL, NULL );
		WideCharToMultiByte( CP_ACP, 0, _w_text, -1, text, sizeof(text), NULL, NULL );

		// log file error
		DWORD dwWritten;
		WriteFile(hLogFile, text, strlen(text), &dwWritten, NULL);
		if (err_no > 0) {
			char *err = strerror(err_no);
			WriteFile(hLogFile, err, strlen(err), &dwWritten, NULL);
		}
		WriteFile(hLogFile, ", ", 2, &dwWritten, NULL);
		WriteFile(hLogFile, filepath, strlen(filepath), &dwWritten, NULL);
		WriteFile(hLogFile, "\r\n", 2, &dwWritten, NULL);
		//delete(&err);
		ReleaseSemaphore(hSemaphoreLog, 1, NULL);
	}
}
// close log file
void logClose() {
	if (hLogFile != NULL) {
		CloseHandle(hLogFile);
		hLogFile = NULL;
	}
}
// open file and read first two bytes.
void openFile(const wchar_t* filepath) {

	FILE *input = _wfopen(filepath, L"rb");
	if (input != NULL)
	{
		if (_logging)
		{
			logError(filepath, L"Info open file: ", 0);
		}

		// read 2 bytes and close the file
		if (_read_file)
		{
			BYTE* buffer[2];
			fread(buffer, 1, 2, input);
		}

		// close file
		fclose(input);
		
		// count files
		files_opened++;
	}
	else
	{
		// count files
		files_error++;

		// some error occured, log that file
		logError(filepath, L"Error open file: ", errno);

	}
}
// threads method that opens the file
unsigned __stdcall ThreadProc(void *dummy) {
	while (run) {
		
		// wait until main thread signals to process next file
		DWORD result = WaitForSingleObject( hSemaphoreFile, INFINITE );
		
		if (run)
		{
			running_threads_same_time++;
			if (running_threads_same_time > running_threads_same_time_max)
				running_threads_same_time_max = running_threads_same_time;
				
			// copy global file name to local variable
			wchar_t* file = new wchar_t[512];
			wcscpy(file, file_threads);
			
			// signal the main thread to continue
			SetEvent(hProcessNextEvent);

			// open the file
			openFile(file);
			delete(file);
			
			running_threads_same_time--;
		}
		else
		{
			// signal other threads to terminate
			ReleaseSemaphore(hSemaphoreFile, 1, NULL);
		}
	}
	running_threads--;
}
// process the file (call openFile)
// create a thread if max number isn't reached
void processFile(const wchar_t* file)
{
	files_found++;

	if (max_threads > 0)
	{
		// set threads file
		file_threads = file;
		
		// create thread if needed
		if (running_threads < max_threads) {
			running_threads++;
			unsigned threadID;
		 	threads[running_threads - 1] = (HANDLE)_beginthreadex(NULL, 0, &ThreadProc, NULL, 0, &threadID);
		}
		
		// trigger a thread to process the file
		ReleaseSemaphore(hSemaphoreFile, 1, NULL);
		
		// wait until thread got the file
		WaitForSingleObject(hProcessNextEvent, INFINITE);
		ResetEvent(hProcessNextEvent);
	}
	else
	{
		// no threads, so open the file
		openFile(file);
	}
}
// processes all elements of path and calls itself recursivly for directories
void processPath(const wchar_t* dir)
{
	// counter for multi machine mode
	long counter = -1;

	// increase path depth
	path_depth++;

	// set pattern
	wstring findpath = wstring(dir);
	findpath.append(L"\\*");

	WIN32_FIND_DATA finddata;
	HANDLE findfile = FindFirstFile(findpath.c_str(), &finddata);
	wchar_t* last_found;
	if (findfile != INVALID_HANDLE_VALUE)
	{
		vector<WIN32_FIND_DATA> files;
		
		do
		{
			if (wcscmp(finddata.cFileName, L".") != 0 && wcscmp(finddata.cFileName, L"..") != 0)
			{
				last_found = finddata.cFileName;
				
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
					// add to vector	
					files.push_back(finddata);
				}
			}
		} while (FindNextFile(findfile, &finddata));
		
		DWORD err = GetLastError();
		if (err != ERROR_NO_MORE_FILES)
		{
			logError(dir, wstring(L"Error getting next file (last found was ").append(last_found).append(L"): ").c_str(), err);
		}
		
		// close find file handle
		FindClose(findfile);
		
		// now process the files and directories
		while (files.size() > 0) {
			
			// get first file
			vector<WIN32_FIND_DATA>::iterator iterator = files.begin();
			
			// get full file path
			wchar_t* fname = (*iterator).cFileName;
			wstring file = wstring(dir).append(L"\\").append(fname);
			
			if (((*iterator).dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
				// it's a file, process it
				if (ext_list != NULL)
				{
					wchar_t* dot = wcsrchr(fname, L'.');
					if (dot != NULL) {
						int l = wcslen(fname) - (dot - fname);
						wchar_t* ext = new wchar_t[l + 3];
						ext[0] = ',';
						ext[1] = '\0';
						wcsncpy(ext + 1, dot + 1, l);
						ext = wcscat(ext, L",");
						tolower(ext);
						if (wcsstr(ext_list, ext) != NULL)
							processFile(file.c_str());
						else {
							//char *tmp = new char[256]; wcstombs(tmp, fname, 256); cout << tmp << "\n";
							//wcstombs(tmp, ext, 256); cout << tmp << "\n";
						}
						free(ext);
					}
				} else {
					processFile(file.c_str());
				}
			} else {
				// it's a directory, recursively search it
				processPath(file.c_str());
			}
			
			// remove file from vector
			files.erase(iterator);
		}
	}
	else
	{
		logError(dir, L"Unable to scan path: ", GetLastError());
	}

	// decrease path depth
	path_depth--;
}
int main(int argc, char *argv[])
{
    //system("PAUSE");
	//_set_error_mode(_OUT_TO_STDERR);

	cout << "Filer Virusscan Trigger v." << VER_MAJOR << "." << VER_BUILD << " (c) Network Appliance 2005\n";
	
	// wide char conversion
	wchar_t exe[strlen(argv[0])+1];
	// convert executable path
	MultiByteToWideChar( CP_ACP, 0, argv[0], strlen(argv[0])+1, exe, sizeof(exe)/sizeof(exe[0]) );

	// set log file name
	sLogFile.assign(exe).append(L".error.log");

	if (argc < 2) {
		cout << "\n";
		cout << "Access all files of a path by opening them and reading the first two bytes.\n";
		cout << "A log file is generated for those files that were unable to access.\n";
		cout << "\n";
		cout << "Missing parameter.\n";
		cout << "Usage: " << argv[0] << " path [threads [machine_count remainder [ext1[,ext2[,..]]]]]\n";
		cout << "threads: How many threads used for accessing files, default is 10, max is 64, 0=none.\n";
		cout << "\n";
		cout << "Multi machine mode, by default off (available for first path level):\n";
		cout << "In this mode the counter for found path entries is divided by machine_count.\n";
		cout << "The remainder is 0..machine_count - 1 and if it equals for this machine,\n";
		cout << "the file or path is processed.\n";
		cout << "After the remainder an file extension list seperated by comma can be added.\n";
		cout << "If that list is specified, files with only that extension will be processed.\n";
		return EXIT_FAILURE;
	}

	// convert path
	wchar_t path[strlen(argv[1])+1];
	MultiByteToWideChar( CP_ACP, 0, argv[1], strlen(argv[1])+1, path, sizeof(path)/sizeof(path[0]) );

	// get number of threads used for accessing the files, default is 10
	if (argc > 2)
	{
		max_threads = atoi(argv[2]);
		if (max_threads > 64)
		{
			max_threads = 64;
		}
		if (argc > 4)
		{
			client_count = atoi(argv[3]);
			client_id = atoi(argv[4]);
			// adjust log file name
			wchar_t id[10];
			_itow(client_id, id, 10);
			sLogFile.assign(exe).append(L".error.").append(id).append(L".log");
			
			if (argc > 5)
			{
				ext_list = new wchar_t[255];
				ext_list[0] = L',';
				int s = strlen(argv[5]);
				mbstowcs(ext_list + 1, argv[5], 255);
				if (ext_list[s + 1] != L',') {
					ext_list[s + 1] = L',';
					ext_list[s + 2] = L'\0';
				}
				tolower(ext_list);
			}
		}
	}

	hProcessNextEvent = CreateEvent( 
	    NULL,           // no security attributes
	    TRUE,           // manual-reset event
	    FALSE,          // initial state is not signaled
	    NULL  			// unnamed object
	);
	hSemaphoreFile = CreateSemaphore( 
	    NULL,   // no security attributes
	    1,   // initial count
	    1,   // maximum count
	    NULL);  // unnamed semaphore
	hSemaphoreLog = CreateSemaphore( 
	    NULL,   // no security attributes
	    1,   // initial count
	    1,   // maximum count
	    NULL);  // unnamed semaphore

	// delete log file
	logDelete();

	// get start time
	DWORD startTime = GetTickCount();
	
	// initialize threads array
	if (max_threads > 0) {
		threads = new HANDLE[max_threads];
	}

	// remove tailing \ if exists
	int l = wcslen(path);
	if (path[l - 1] == '\\')
	{
		path[l - 1] = '\0';
	}

	// start processing this path
	processPath(path);
	
	// all files had been found, wait untill all files had been processed
	run = false;
	
	if (files_found > 0)
	{
		// print files found
		cout << files_found << " files found.\n";
	
		// main loop that waits till all files had been processed and stack is empty
		if (running_threads > 0)
		{
			// signal the threads to return
			ReleaseSemaphore(hSemaphoreFile, 1, NULL);
			
			// wait until all threads finished, wait not longer than 60 sec.
			WaitForMultipleObjects( running_threads, threads, TRUE, 60000 );
		}
	
		DWORD endTime = GetTickCount();
		DWORD time = endTime - startTime;
		if (endTime < startTime)
		{
			// oops, during run the timer had been set to 0, so get real time diff
			time = endTime + 0xFFFFFFFF - startTime;
		}
	
		// print statistic
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
		// print max thread load if lower than max_threads	
		if (max_threads > 0 && running_threads_same_time_max < max_threads)
		{
			cout << running_threads_same_time_max << " threads had been executed at the same time.\n";
		}
	} 
	else
	{
		cout << "No files found.\n";
	}
	
	// close log file
	logClose();

    return EXIT_SUCCESS;
}
