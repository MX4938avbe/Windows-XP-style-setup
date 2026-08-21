#include <iostream>
#include <string>
#include <windows.h>
#include <conio.h>
#include <vector>
#include <commdlg.h>
#include <fstream>
#include <cstdlib>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Version.lib")

using namespace std;

// ==================== 1. Structs ====================
struct InstalledOS {
    string path;
    string name;
};

struct PartitionInfo {
    string driveLetter;
    string fsType;
    ULONGLONG totalMB;
    ULONGLONG freeMB;
};

// ==================== 2. Declarations ====================
vector<wstring> g_customDriverPaths;

// App Core Functions
void RunDiskpartScript(const string& script);
string GetOSNameFromDLL(const string& dllPath);
bool RunCommandSync(const string& cmd);
string FindInstallImage();
bool ApplyImageToTarget(const string& targetDriveLetter);
vector<InstalledOS> ScanInstalledOS();
vector<PartitionInfo> ScanPartitions();
void RunCommand(const char* cmd);
bool HasInstalledOS(const string& driveLetter);
void InjectDriversToInstalledOS(const string& targetDriveLetter);
bool ApplyRegistryFix(const string& targetDriveLetter, bool fullAuto = true);

// UI Drawing
void GotoXY(short x, short y);
void DrawStatusBar(const string& text);
void makeTextHighlighted(const string& text);
void DrawBox(short x, short y, short w, short h);
void DrawOSItem(short x, short y, short maxLen, const string& path, const string& osName, bool isSelected);
void DrawPartitionItem(short x, short y, short maxLen, const PartitionInfo& part, bool isSelected);
void SetupRetroConsole();
void setupStart();
bool LoadDriverFromF6();
bool showQuitConfirmPopup();
void openRecoveryConsoleCMD();

// Pages
void mainPage();
void LicensePage();
void repOS();
void chooseHardDrive();
void notArrangedDrive(const PartitionInfo& part);
void deleteSystemPartitionWarnPage();
void confirmDeletePartitionPage(const PartitionInfo& part);
void createPartitionPage(ULONGLONG diskTotalMB, ULONGLONG maxMB, const string& inputSize);
void DrawFormatOptionItem(short x, short y, const string& text, bool isSelected);
void showFormattingProgressPage(const PartitionInfo& part, int percent);
void drawFileCopyShell();
void updateFileCopyBar(int percent);
void showSavingSettings(const string& targetDriveLetter);
void showRebootCountdownPage(bool isSuccess);

// Controller
bool WaitForKey(DWORD timeoutMs, int targetExtKey);
void mainPageKey();
bool confirmDeletePartitionKey();
void LicensePageKey();
bool deleteSystemPartitionWarnKey();
ULONGLONG createPartitionKey(ULONGLONG diskTotalMB, ULONGLONG maxMB);
int notArrangedDriveKey(const PartitionInfo& part);
void runChooseHardDriveProcess();

// ==================== 3. App Core Functions ====================

void RunDiskpartScript(const string& script) {
    {
        ofstream file("X:\\dp_cmd.txt", ios::trunc);
        if (file.is_open()) {
            file << script;
            file.flush();
            file.close();
        }
    }
    system("diskpart /s X:\\dp_cmd.txt > NUL");
    system("wpeutil UpdateBootInfo");
}

string GetOSNameFromDLL(const string& dllPath) {
    DWORD dummy;
    DWORD size = GetFileVersionInfoSizeA(dllPath.c_str(), &dummy);
    if (size == 0) return "Microsoft Windows";

    string data(size, 0);
    if (!GetFileVersionInfoA(dllPath.c_str(), 0, size, &data[0])) return "Microsoft Windows";

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT len = 0;
    if (VerQueryValueA(&data[0], "\\", (LPVOID*)&fileInfo, &len) && fileInfo) {
        DWORD major = HIWORD(fileInfo->dwFileVersionMS);
        DWORD minor = LOWORD(fileInfo->dwFileVersionMS);

        if (major == 6 && minor == 2) {
            DWORD build = HIWORD(fileInfo->dwFileVersionLS);

            if (build >= 20000)
                return "Windows 11";
            else if (build >= 10240)
                return "Windows 10";
            else if (build >= 9887)
                return "Windows 10 Technical Preview";
            else if (build >= 9796)
                return "Windows Technical Review";
            else if (build >= 9300)
                return "Windows 8.1";
            else if (build >= 8250)
                return "Windows 8";
            else return "Windows Developer Preview";
        }

        if (major == 3 && minor == 1) return "Microsoft Windows NT";
        if (major == 3 && minor == 50 || minor == 5) return "Microsoft Windows NT 3.5";
        if (major == 3 && minor == 51) return "Microsoft Windows NT 3.51";
        if (major == 4 && minor == 0) return "Microsoft Windows NT 4.0 Professional";
        if (major == 5 && minor == 0) return "Microsoft Windows 2000 Professional";
        if (major == 5 && minor == 1) return "Microsoft Windows XP Professional";
        if (major == 5 && minor == 2) return "Microsoft Windows Server 2003";
        if (major == 6 && minor == 0) return "Microsoft Windows Vista";
        if (major == 6 && minor == 1) return "Windows 7";
        // DO NOT USE abovementioned major/minor logic as MS has depreciated it since Windows 8.1!!!!!
    }
    return "Microsoft Windows Operating System";
}

bool RunCommandSync(const string& cmd) {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmdBuffer[MAX_PATH * 2];
    strcpy_s(cmdBuffer, cmd.c_str());

    if (CreateProcessA(NULL, cmdBuffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

string FindInstallImage() {
    char drives[256];
    DWORD len = GetLogicalDriveStringsA(sizeof(drives), drives);
    if (len == 0) return "";

    char* drive = drives;
    while (*drive) {
        string wimPath = string(drive) + "sources\\install.wim";
        string esdPath = string(drive) + "sources\\install.esd";

        if (GetFileAttributesA(wimPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return wimPath;
        }
        if (GetFileAttributesA(esdPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return esdPath;
        }
        drive += strlen(drive) + 1;
    }
    return "";
}

vector<InstalledOS> ScanInstalledOS() {
    vector<InstalledOS> osList;
    char drives[256];
    DWORD len = GetLogicalDriveStringsA(sizeof(drives), drives);
    if (len == 0) return osList;

    char* drive = drives;
    while (*drive) {
        if (toupper(drive[0]) != 'X') {
            string winDir = string(drive) + "Windows";
            string dllPath = winDir + "\\System32\\kernel32.dll";

            DWORD attr = GetFileAttributesA(dllPath.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES) {
                InstalledOS os;
                os.path = string(1, toupper(drive[0])) + ":\\WINDOWS";
                os.name = GetOSNameFromDLL(dllPath);
                osList.push_back(os);
            }
        }
        drive += strlen(drive) + 1;
    }
    return osList;
}

vector<PartitionInfo> ScanPartitions() {
    vector<PartitionInfo> list;
    char drives[256];
    DWORD len = GetLogicalDriveStringsA(sizeof(drives), drives);
    if (len == 0) return list;

    char* drive = drives;
    while (*drive) {
        if (toupper(drive[0]) != 'X') {
            char fsName[MAX_PATH] = { 0 };
            GetVolumeInformationA(drive, NULL, 0, NULL, NULL, NULL, fsName, sizeof(fsName));

            ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
            if (GetDiskFreeSpaceExA(drive, &freeBytes, &totalBytes, &totalFreeBytes)) {
                PartitionInfo info;
                info.driveLetter = string(1, toupper(drive[0])) + ":";
                info.fsType = (strlen(fsName) > 0) ? fsName : "RAW";
                info.totalMB = totalBytes.QuadPart / (1024 * 1024);
                info.freeMB = totalFreeBytes.QuadPart / (1024 * 1024);
                list.push_back(info);
            }
        }
        drive += strlen(drive) + 1;
    }
    return list;
}

void RunCommand(const char* cmd) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmdBuffer[MAX_PATH * 2];
    strcpy_s(cmdBuffer, cmd);

    if (CreateProcessA(NULL, cmdBuffer, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

bool HasInstalledOS(const string& driveLetter) {
    vector<InstalledOS> osList = ScanInstalledOS();
    for (const auto& os : osList) {
        if (os.path.substr(0, driveLetter.length()) == driveLetter) {
            return true;
        }
    }
    return false;
}

void InjectDriversToInstalledOS(const string& targetDriveLetter) {
    if (g_customDriverPaths.empty()) return;

    for (const auto& driverPath : g_customDriverPaths) {
        wstring wTargetDrive(targetDriveLetter.begin(), targetDriveLetter.end());
        wstring cmdLine = L"dism.exe /Image:" + wTargetDrive + L"\\ /Add-Driver /Driver:\"" + driverPath + L"\"";

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        wchar_t cmdBuffer[MAX_PATH * 2];
        wcscpy_s(cmdBuffer, cmdLine.c_str());

        if (CreateProcessW(NULL, cmdBuffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
}

bool ApplyRegistryFix(const string& targetDriveLetter, bool fullAuto) {
    string targetSys = targetDriveLetter + ":\\Windows\\System32\\config\\SYSTEM";
    string targetSoft = targetDriveLetter + ":\\Windows\\System32\\config\\SOFTWARE";

    string loadSoftCmd = "reg load HKLM\\OFFLINE_SOFTWARE \"" + targetSoft + "\"";
    if (RunCommandSync(loadSoftCmd)) {
        RunCommandSync("reg add HKLM\\OFFLINE_SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System /v VerboseStatus /t REG_DWORD /d 1 /f");

        RunCommandSync("reg add HKLM\\OFFLINE_SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System /v EnableCursorSuppression /t REG_DWORD /d 0 /f");

        RunCommandSync("reg unload HKLM\\OFFLINE_SOFTWARE");
    }
    string loadSysCmd = "reg load HKLM\\OFFLINE_SYSTEM \"" + targetSys + "\"";
    if (RunCommandSync(loadSysCmd)) {
        if (!fullAuto) {
            RunCommandSync("reg add HKLM\\OFFLINE_SYSTEM\\Setup /v CmdLine /t REG_SZ /d \"cmd.exe\" /f");
            RunCommandSync("reg add HKLM\\OFFLINE_SYSTEM\\Setup /v SetupType /t REG_DWORD /d 2 /f");
        }
        else {
            RunCommandSync("reg add HKLM\\OFFLINE_SYSTEM\\Setup /v OOBEInProgress /t REG_DWORD /d 0 /f");
            RunCommandSync("reg add HKLM\\OFFLINE_SYSTEM\\Setup /v SetupType /t REG_DWORD /d 0 /f");
            RunCommandSync("reg add HKLM\\OFFLINE_SYSTEM\\Setup /v SystemSetupInProgress /t REG_DWORD /d 0 /f");

            RunCommandSync("reg add \"HKLM\\OFFLINE_SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\" /v AutoAdminLogon /t REG_SZ /d 1 /f");
        }

        RunCommandSync("reg unload HKLM\\OFFLINE_SYSTEM");
    }

    return true;
}

// ==================== 4. UI Drawing ====================
void GotoXY(short x, short y) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos = { x, y };
    SetConsoleCursorPosition(hOut, pos);
}

void DrawStatusBar(const string& text) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GotoXY(0, 24);
    SetConsoleTextAttribute(hOut, 0x70);

    string fullText = text;
    if (fullText.length() < 79) {
        fullText.append(80 - fullText.length(), ' ');
    }
    else {
        fullText = fullText.substr(0, 79);
    }

    cout << fullText;
    SetConsoleTextAttribute(hOut, 0x17);
}

void makeTextHighlighted(const string& text) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hOut, 0x1F);
    cout << text;
    SetConsoleTextAttribute(hOut, 0x17);
}

void DrawBox(short x, short y, short w, short h) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hOut, 0x17);

    GotoXY(x, y); cout << (char)218;
    for (int i = 0; i < w - 2; i++) cout << (char)196;
    cout << (char)191;

    for (int i = 1; i < h - 1; i++) {
        GotoXY(x, y + i); cout << (char)179;
        GotoXY(x + w - 1, y + i); cout << (char)179;
    }

    GotoXY(x + w - 1, y + h - 1);
    GotoXY(x, y + h - 1); cout << (char)192;
    for (int i = 0; i < w - 2; i++) cout << (char)196;
    cout << (char)217;
}

void DrawOSItem(short x, short y, short maxLen, const string& path, const string& osName, bool isSelected) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GotoXY(x, y);

    string text = " " + path + " \"" + osName + "\"";
    if (text.length() < maxLen) text.append(maxLen - text.length(), ' ');
    else text = text.substr(0, maxLen);

    SetConsoleTextAttribute(hOut, isSelected ? 0x70 : 0x17);
    cout << text;
    SetConsoleTextAttribute(hOut, 0x17);
}

void DrawPartitionItem(short x, short y, short maxLen, const PartitionInfo& part, bool isSelected) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GotoXY(x, y);

    string text = "  " + part.driveLetter + " Partition1 [" + part.fsType + "]   "
        + to_string(part.totalMB) + " MB ( " + to_string(part.freeMB) + " MB free)";

    if (text.length() < maxLen) text.append(maxLen - text.length(), ' ');
    else text = text.substr(0, maxLen);

    SetConsoleTextAttribute(hOut, isSelected ? 0x70 : 0x17);
    cout << text;
    SetConsoleTextAttribute(hOut, 0x17);
}

void SetupRetroConsole() {
    SetConsoleOutputCP(437); //Change this if your language is CJK
    SetConsoleCP(437);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HWND hConsole = GetConsoleWindow();

    int screenX = 0;
    int screenY = 0;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    UINT dpi = GetDpiForWindow(hConsole);
    double dpiScale = dpi / 96.0;

    CONSOLE_FONT_INFOEX cfi = { sizeof(cfi) };
    cfi.cbSize = sizeof(cfi);
    cfi.nFont = 0;
    cfi.dwFontSize.X = (SHORT)((screenW / dpiScale) / 80);
    cfi.dwFontSize.Y = (SHORT)((screenH / dpiScale) / 25);
    cfi.FontFamily = FF_DONTCARE;
    cfi.FontWeight = FW_BOLD;
    wcscpy_s(cfi.FaceName, L"Consolas");
    SetCurrentConsoleFontEx(hOut, FALSE, &cfi);


    //Remove This
    {
        CONSOLE_FONT_INFOEX actualCfi = { sizeof(actualCfi) };
        actualCfi.cbSize = sizeof(actualCfi);
        GetCurrentConsoleFontEx(hOut, FALSE, &actualCfi);

        wofstream dbg(L"X:\\fontdebug.txt", ios::app);
        if (dbg.is_open()) {
            dbg << L"Required Font: " << cfi.FaceName
                << L"  Size: " << cfi.dwFontSize.X << L"x" << cfi.dwFontSize.Y << L"\r\n";
            dbg << L"Actual Font: " << actualCfi.FaceName
                << L"  Size: " << actualCfi.dwFontSize.X << L"x" << actualCfi.dwFontSize.Y << L"\r\n";
            dbg << L"dpi=" << dpi << L" dpiScale=" << dpiScale
                << L" screenW=" << screenW << L" screenH=" << screenH << L"\r\n\r\n";
            dbg.close();
        }
    }


    SMALL_RECT minWindow = { 0, 0, 1, 1 };
    SetConsoleWindowInfo(hOut, TRUE, &minWindow);

    COORD bufferSize = { 80, 25 };
    SetConsoleScreenBufferSize(hOut, bufferSize);

    SMALL_RECT windowSize = { 0, 0, 79, 24 };
    SetConsoleWindowInfo(hOut, TRUE, &windowSize);

    LONG_PTR style = GetWindowLongPtr(hConsole, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    SetWindowLongPtr(hConsole, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtr(hConsole, GWL_EXSTYLE);
    exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
    SetWindowLongPtr(hConsole, GWL_EXSTYLE, exStyle);

    SetWindowPos(hConsole, HWND_TOPMOST, screenX, screenY, screenW, screenH,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hOut, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &cursorInfo);

    SetConsoleTextAttribute(hOut, 0x17);
    system("cls");
}


void setupStart() {
    SetupRetroConsole();

    GotoXY(0, 1);  cout << "Windows Setup";
    GotoXY(0, 2);  cout << string(15, (char)205);

    DrawStatusBar(" Press F6 if you need to install a third party SCSI or RAID driver...");
    bool f6Pressed = WaitForKey(3000, 64);
    if (f6Pressed) {
        if (LoadDriverFromF6())
            RunCommand("wpeutil UpdateBootInfo");
    }

    DrawStatusBar(" Press F2 to run Automated System Recovery (ASR)...");
    bool f2Pressed = WaitForKey(3000, 60);
    if (f2Pressed) {
        RunCommand("X:\\sources\\recovery\\StartRep.exe");
    }

    DrawStatusBar(" Setup is starting Windows");
    Sleep(250);
}

bool LoadDriverFromF6() {
    SetupRetroConsole();
    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);
    GotoXY(3, 5);  cout << "Please select the driver INF file to load...";
    DrawStatusBar(" Select Driver File...");

    WCHAR szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetConsoleWindow();
    ofn.lpstrFilter = L"Setup Information (*.inf)\0*.inf\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"Select Driver INF";

    if (GetOpenFileNameW(&ofn)) {
        wstring cmdLine = L"drvload.exe \"" + wstring(szFile) + L"\"";

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        wchar_t cmdBuffer[MAX_PATH * 2];
        wcscpy_s(cmdBuffer, cmdLine.c_str());

        if (CreateProcessW(NULL, cmdBuffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            g_customDriverPaths.push_back(szFile);
            return true;
        }
    }
    return false;
}

bool showQuitConfirmPopup() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD alertAttr = 0x4F;

    short boxX = 19, boxY = 9, boxW = 42, boxH = 9;

    SetConsoleTextAttribute(hOut, alertAttr);
    GotoXY(boxX, boxY); cout << (char)201;
    for (int i = 0; i < boxW - 2; i++) cout << (char)205;
    cout << (char)187;

    for (int i = 1; i < boxH - 1; i++) {
        GotoXY(boxX, boxY + i); cout << (char)186;
        for (int j = 0; j < boxW - 2; j++) cout << " ";
        cout << (char)186;
    }

    GotoXY(boxX, boxY + boxH - 1); cout << (char)200;
    for (int i = 0; i < boxW - 2; i++) cout << (char)205;
    cout << (char)188;

    GotoXY(boxX + 2, boxY + 1); cout << "Windows XP is not completely set up on";
    GotoXY(boxX + 2, boxY + 2); cout << "your computer.  If you quit Setup now,";
    GotoXY(boxX + 2, boxY + 3); cout << "you will need to run Setup again to set";
    GotoXY(boxX + 2, boxY + 4); cout << "up Windows XP.";

    GotoXY(boxX + 4, boxY + 6); cout << "\x07  To continue Setup, press ENTER.";
    GotoXY(boxX + 4, boxY + 7); cout << "\x07  To quit Setup, press F3.";

    DrawStatusBar(" F3=Quit  ENTER=Continue");

    while (true) {
        int key = _getch();
        if (key == 13) {
            SetConsoleTextAttribute(hOut, 0x17);
            return false;
        }
        else if (key == 0 || key == 224) {
            int extKey = _getch();
            if (extKey == 61) {
                SetConsoleTextAttribute(hOut, 0x17);
                return true;
            }
        }
    }
}

void openRecoveryConsoleCMD() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hOut, 0x07);
    system("cls");

    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hOut, &cursorInfo);
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hOut, &cursorInfo);

    cout << "Microsoft Windows XP(TM) Recovery Console.\n\n";
    cout << "The Recovery Console provides system repair and recovery functionality.\n\n";
    cout << "Type EXIT to quit the Recovery Console and restart the computer.\n\n";
    cout << "The path or file specified is not valid.";

    system("cmd.exe /k cd /d X:\\Windows\\system32 && prompt $P$G");
    exit(0);
}

void mainPage() {
    SetupRetroConsole();
    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(3, 4);  makeTextHighlighted("Welcome to Setup.");
    GotoXY(3, 6);  cout << "This portion of the Setup program prepares Microsoft(R)";
    GotoXY(3, 7);  cout << "Windows(R) XP to run on your computer.";
    GotoXY(6, 10); cout << "�  To set up Windows XP now, press ENTER.";
    GotoXY(6, 12); cout << "�  To repair a Windows XP installation using";
    GotoXY(9, 13); cout << "Recovery Console, press R.";
    GotoXY(6, 15); cout << "�  To quit setup without installing Windows XP, press F3.";

    DrawStatusBar(" ENTER=Continue  R=Repair  F3=Quit");
}

void LicensePage() {
    SetupRetroConsole();
    GotoXY(0, 1);  cout << " Windows XP Licensing Agreement";
    GotoXY(0, 2);  cout << string(32, (char)205);

    GotoXY(3, 4);  cout << "END-USER LICENSING AGREEMENT FOR MICROSOFT";
    GotoXY(3, 5);  cout << "SOFTWARE";
    GotoXY(3, 7);  cout << "MICROSOFT WINDOWS XP PROFESSIONAL EDITION";
    GotoXY(3, 8);  cout << "SERVICE PACK 3";
    GotoXY(3, 10); cout << "Your use of this software is subject to the terms and conditions";
    GotoXY(3, 11); cout << "of the license agreement by which you acquired this software.";
    GotoXY(3, 12); cout << "If you are a volume license customer, use of this software is";
    GotoXY(3, 13); cout << "subject to your volume license agreement.";

    DrawStatusBar(" F8=I agree  ESC=I do not agree");
}

void repOS() {
    SetupRetroConsole();
    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(3, 4); cout << "If one of the following Windows XP installations is damaged,";
    GotoXY(3, 5); cout << "Setup can try to repair it.";
    GotoXY(3, 7); cout << "Use the UP and DOWN arrow keys to select an item in the list.";
    GotoXY(6, 9); cout << "�  To repair the selected Windows installation, Press R.";
    GotoXY(6, 11); cout << "�  To continue installing a fresh copy of Windows XP";
    GotoXY(9, 12); cout << "without repairing, press ESC.";

    DrawBox(3, 14, 74, 8);
    DrawStatusBar(" F3=Quit  R=Repair  ESC=Don't Repair");
}

void chooseHardDrive() {
    SetupRetroConsole();
    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(3, 4); cout << "The following list shows the existing partitions and";
    GotoXY(3, 5); cout << "unpartitioned space on this computer.";
    GotoXY(3, 7); cout << "Use the UP and DOWN arrow keys to select an item in the list.";
    GotoXY(6, 9); cout << "�  To set up Windows XP on the selected item, press ENTER.";
    GotoXY(6, 11); cout << "�  To create a partition in the unpartitioned space, press C.";
    GotoXY(6, 13); cout << "�  To delete the selected partition, press D.";

    DrawBox(3, 13, 74, 9);
    DrawStatusBar(" ENTER=Install  C=Create Partition  D=Delete Partition   F3=Quit");
}

void notArrangedDrive(const PartitionInfo& part) {
    SetupRetroConsole();
    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(3, 4); cout << "A new partition for Windows has been created on";
    GotoXY(3, 6); cout << part.totalMB << " MB Disk 0 at Id 0 on bus 0 on atapi [MBR].";

    GotoXY(3, 8); cout << "This partition must now be formatted.";
    GotoXY(3, 10); cout << "From the list below, select a file system for the new partition.";
    GotoXY(3, 11); cout << "Use the UP and DOWN arrow keys to select the file system you want,";
    GotoXY(3, 12); cout << "and then press ENTER.";

    GotoXY(3, 14); cout << "If you want to select a different partition for Windows XP,";
    GotoXY(3, 15); cout << "Press ESC.";

    DrawStatusBar(" ENTER=Continue  ESC=Cancel");
}

void deleteSystemPartitionWarnPage() {
    SetupRetroConsole();

    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(3, 4);  cout << "The partition you tried to delete is a system partition.";

    GotoXY(3, 6);  cout << "System partitions may contain diagnostic or hardware configuration";
    GotoXY(3, 7);  cout << "programs, programs to start operating systems (such as Windows XP),";
    GotoXY(3, 8);  cout << "or other manufacturer-supplied programs.";

    GotoXY(3, 10); cout << "Delete a system partition only if you are sure that it contains";
    GotoXY(3, 11); cout << "no such programs or if you are willing to lose them. Deleting a";
    GotoXY(3, 12); cout << "system partition may prevent your computer from starting from";
    GotoXY(3, 13); cout << "the hard disk until you complete installation of Windows XP.";

    GotoXY(6, 15); cout << "�  To delete this partition, press ENTER.";
    GotoXY(9, 16); cout << "Setup will prompt you for confirmation before";
    GotoXY(9, 17); cout << "deleting the partition.";

    GotoXY(6, 19); cout << "�  To go back to the previous screen without";
    GotoXY(9, 20); cout << "deleting the partition, press ESC.";

    DrawStatusBar(" ENTER=Continue  ESC=Cancel");
}

void confirmDeletePartitionPage(const PartitionInfo& part) {
    SetupRetroConsole();

    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(3, 4);  cout << "You asked Setup to delete the partition";

    GotoXY(7, 6);
    cout << part.driveLetter << "  Partition1 [" << part.fsType << "]";

    GotoXY(42, 6);
    cout << part.totalMB << " MB ( " << part.freeMB << " MB free)";

    GotoXY(3, 8);
    cout << "on " << part.totalMB << " MB Disk 0 at Id 0 on bus 0 on atapi [MBR].";

    GotoXY(6, 11); cout << "\x07  To delete this partition, press L.";
    GotoXY(9, 12); cout << "CAUTION: All data on this partition will be lost.";

    GotoXY(6, 14); cout << "\x07  To return to the previous screen without";
    GotoXY(9, 15); cout << "deleting the partition, press ESC.";

    DrawStatusBar(" L=Delete  ESC=Cancel");
}

void createPartitionPage(ULONGLONG diskTotalMB, ULONGLONG maxMB, const string& inputSize) {
    SetupRetroConsole();

    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(3, 4);  cout << "You asked Setup to create a new partition on";
    GotoXY(3, 5);  cout << diskTotalMB << " MB Disk 0 at Id 0 on bus 0 on atapi [MBR].";

    GotoXY(6, 7);  cout << "\x07  To create the new partition, enter a size below and";
    GotoXY(9, 8);  cout << "press ENTER.";

    GotoXY(6, 10); cout << "\x07  To go back to the previous screen without creating";
    GotoXY(9, 11); cout << "the partition, press ESC.";

    GotoXY(3, 14); cout << "The minimum size for the new partition is       8 megabytes (MB).";
    GotoXY(3, 15); cout << "The maximum size for the new partition is  " << maxMB << " megabytes (MB).";

    GotoXY(3, 16); cout << "Create partition of size (in MB):  ";

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hOut, 0x70);
    string displayVal = inputSize;
    if (displayVal.length() < 7) displayVal.append(7 - displayVal.length(), ' ');
    cout << displayVal;
    SetConsoleTextAttribute(hOut, 0x17);

    DrawStatusBar(" ENTER=Create  ESC=Cancel");
}

void DrawFormatOptionItem(short x, short y, const string& text, bool isSelected) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GotoXY(x, y);

    string displayText = text;
    if (displayText.length() < 65) {
        displayText.append(65 - displayText.length(), ' ');
    }

    SetConsoleTextAttribute(hOut, isSelected ? 0x70 : 0x17);
    cout << displayText;
    SetConsoleTextAttribute(hOut, 0x17);
}

int notArrangedDriveKey(const PartitionInfo& part) {
    vector<string> options;

    if (part.totalMB > 32768) {
        options = {
            "Format the partition using the NTFS file system (Quick)",
            "Format the partition using the NTFS file system",
            "Leave the current file system intact (no changes)"
        };
    }
    else {
        options = {
            "Format the partition using the NTFS file system (Quick)",
            "Format the partition using the FAT file system (Quick)",
            "Format the partition using the NTFS file system",
            "Format the partition using the FAT file system",
            "Leave the current file system intact (no changes)"
        };
    }

    int selectedIndex = (part.fsType == "RAW" || part.fsType.empty()) ? 0 : (int)options.size() - 1;

    while (true) {
        notArrangedDrive(part);

        for (size_t i = 0; i < options.size(); i++) {
            DrawFormatOptionItem(6, 17 + (short)i, options[i], (i == selectedIndex));
        }

        DrawStatusBar(" ENTER=Continue  ESC=Cancel");

        int key = _getch();
        if (key == 0 || key == 224) {
            int extKey = _getch();
            if (extKey == 72 && selectedIndex > 0) {
                selectedIndex--;
            }
            else if (extKey == 80 && selectedIndex < (int)options.size() - 1) {
                selectedIndex++;
            }
        }
        else if (key == 13) {
            return selectedIndex;
        }
        else if (key == 27) {
            return -1;
        }
    }
}

void showFormattingProgressPage(const PartitionInfo& part, int percent) {
    SetupRetroConsole();
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(17, 5); cout << "Please wait while Setup formats the partition";

    GotoXY(7, 7);
    cout << part.driveLetter << "  Partition1 [" << part.fsType << "]";
    GotoXY(46, 7);
    cout << part.totalMB << " MB ( " << part.freeMB << " MB free)";

    GotoXY(14, 9);
    cout << "on " << part.totalMB << " MB Disk 0 at Id 0 on bus 0 on atapi [MBR].";

    short boxX = 7, boxY = 15, boxW = 64, boxH = 6;
    GotoXY(boxX, boxY); cout << (char)201;
    for (int i = 0; i < boxW - 2; i++) cout << (char)205;
    cout << (char)187;

    for (int i = 1; i < boxH; i++) {
        GotoXY(boxX, boxY + i); cout << (char)186;
        GotoXY(boxX + boxW - 1, boxY + i); cout << (char)186;
    }

    GotoXY(boxX, boxY + boxH); cout << (char)200;
    for (int i = 0; i < boxW - 2; i++) cout << (char)205;
    cout << (char)188;

    GotoXY(9, 16); cout << "Setup is formatting...";

    short barX = 17, barY = 19, barW = 46;
    GotoXY(barX - 1, barY - 1); cout << (char)218;
    for (int i = 0; i < barW; i++) cout << (char)196;
    cout << (char)191;

    GotoXY(barX - 1, barY); cout << (char)179;
    GotoXY(barX + barW, barY); cout << (char)179;

    GotoXY(barX - 1, barY + 1); cout << (char)192;
    for (int i = 0; i < barW; i++) cout << (char)196;
    cout << (char)217;

    DrawStatusBar("");

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    GotoXY(boxX + (boxW / 2) - 2, 17);
    if (percent < 10) cout << " ";
    cout << percent << "%";

    int fillLen = (percent * barW) / 100;

    GotoXY(barX, barY);
    SetConsoleTextAttribute(hOut, 0x1E);
    for (int i = 0; i < fillLen; i++) {
        cout << (char)219;
    }
    SetConsoleTextAttribute(hOut, 0x17);
}

void drawFileCopyShell() {
    SetupRetroConsole();
    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(22, 6); cout << "Please wait while Setup copies files";
    GotoXY(22, 7); cout << "to the Windows installation folders.";
    GotoXY(18, 8); cout << "This might take several minutes to complete.";

    short boxX = 7, boxY = 11, boxW = 64, boxH = 6;
    GotoXY(boxX, boxY); cout << (char)201;
    for (int i = 0; i < boxW - 2; i++) cout << (char)205;
    cout << (char)187;

    for (int i = 1; i < boxH; i++) {
        GotoXY(boxX, boxY + i); cout << (char)186;
        GotoXY(boxX + boxW - 1, boxY + i); cout << (char)186;
    }

    GotoXY(boxX, boxY + boxH); cout << (char)200;
    for (int i = 0; i < boxW - 2; i++) cout << (char)205;
    cout << (char)188;

    GotoXY(9, 12); cout << "Setup is copying files...";

    short barX = 17, barY = 15, barW = 46;
    GotoXY(barX - 1, barY - 1); cout << (char)218;
    for (int i = 0; i < barW; i++) cout << (char)196;
    cout << (char)191;

    GotoXY(barX - 1, barY); cout << (char)179;
    GotoXY(barX + barW, barY); cout << (char)179;
    GotoXY(barX - 1, barY + 1); cout << (char)192;
    for (int i = 0; i < barW; i++) cout << (char)196;
    cout << (char)217;

    string statusText = string(57, ' ') + "|Copying: install.wim";
    DrawStatusBar(statusText);
}

void updateFileCopyBar(int percent) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    static int lastPercent = -1;
    if (percent == lastPercent) return;
    lastPercent = percent;

    short boxX = 7, boxW = 64;
    short barX = 17, barY = 15, barW = 46;

    GotoXY(boxX + (boxW / 2) - 2, 13);
    if (percent < 10) cout << " ";
    cout << percent << "%";

    int fillLen = (percent * barW) / 100;

    GotoXY(barX, barY);
    SetConsoleTextAttribute(hOut, 0x1E);
    for (int i = 0; i < fillLen; i++) {
        cout << (char)219;
    }
    SetConsoleTextAttribute(hOut, 0x17);
}

bool ApplyImageToTarget(const string& targetDriveLetter) {
    string imagePath = FindInstallImage();
    if (imagePath.empty()) {
        return false;
    }

    drawFileCopyShell();
    updateFileCopyBar(0);

    string cmd = "dism.exe /Apply-Image /ImageFile:\"" + imagePath +
        "\" /Index:1 /ApplyDir:" + targetDriveLetter + ":\\";

    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.cb = sizeof(si);
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmdBuffer[MAX_PATH * 2];
    strcpy_s(cmdBuffer, cmd.c_str());

    if (!CreateProcessA(NULL, cmdBuffer, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    CloseHandle(hWrite);

    char buffer[256];
    DWORD bytesRead;
    string streamAccumulator = "";

    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        streamAccumulator += buffer;

        size_t percentIdx;
        while ((percentIdx = streamAccumulator.find('%')) != string::npos) {
            size_t start = percentIdx;
            while (start > 0 && (isdigit(streamAccumulator[start - 1]) || streamAccumulator[start - 1] == '.')) {
                start--;
            }
            if (start < percentIdx) {
                string numStr = streamAccumulator.substr(start, percentIdx - start);
                try {
                    float p = stof(numStr);
                    updateFileCopyBar((int)p);
                }
                catch (...) {}
            }
            streamAccumulator = streamAccumulator.substr(percentIdx + 1);
        }

        if (streamAccumulator.length() > 512) {
            streamAccumulator = streamAccumulator.substr(streamAccumulator.length() - 128);
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    if (exitCode == 0) {
        updateFileCopyBar(100);
        Sleep(300);
        return true;
    }

    return false;
}

void showSavingSettings(const string& targetDriveLetter) {
    SetupRetroConsole();
    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    GotoXY(8, 6); cout << "Please wait while Setup initializes your Windows configuration.";

    DrawStatusBar(" Injecting third-party drivers...");
    InjectDriversToInstalledOS(targetDriveLetter);
    ApplyRegistryFix(targetDriveLetter, true);

    DrawStatusBar(" Writing system boot files (BCDBoot)...");
    string bcdCmd = "bcdboot.exe " + targetDriveLetter + ":\\Windows /l zh-TW /f ALL";
    RunCommandSync(bcdCmd);

    Sleep(1000);
}

void showRebootCountdownPage(bool isSuccess) {
    SetupRetroConsole();
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    GotoXY(0, 1);  cout << " Windows XP Professional Setup";
    GotoXY(0, 2);  cout << string(31, (char)205);

    if (isSuccess) {
        GotoXY(3, 5);  cout << "This portion of Setup has completed successfully.";
        GotoXY(3, 7);  cout << "If there is a floppy disk in drive A:, remove it.";
        GotoXY(3, 9);  cout << "To restart your computer, press ENTER.";
        GotoXY(3, 10); cout << "When your computer restarts, Setup will continue.";
    }
    else {
        GotoXY(3, 5);  cout << "Windows XP has not been installed on this computer.";
        GotoXY(3, 7);  cout << "If there is a floppy disk in drive A:, remove it.";
        GotoXY(3, 9);  cout << "To restart your computer, press ENTER.";
    }

    short boxX = 9, boxY = 14, boxW = 62, boxH = 6;
    GotoXY(boxX, boxY); cout << (char)201;
    for (int i = 0; i < boxW - 2; i++) cout << (char)205;
    cout << (char)187;

    for (int i = 1; i < boxH - 1; i++) {
        GotoXY(boxX, boxY + i); cout << (char)186;
        GotoXY(boxX + boxW - 1, boxY + i); cout << (char)186;
    }

    GotoXY(boxX, boxY + boxH - 1); cout << (char)200;
    for (int i = 0; i < boxW - 2; i++) cout << (char)205;
    cout << (char)188;

    short barX = 17, barY = 17, barW = 46;
    GotoXY(barX - 1, barY - 1); cout << (char)218;
    for (int i = 0; i < barW; i++) cout << (char)196;
    cout << (char)191;

    GotoXY(barX - 1, barY); cout << (char)179;
    GotoXY(barX + barW, barY); cout << (char)179;

    GotoXY(barX - 1, barY + 1); cout << (char)192;
    for (int i = 0; i < barW; i++) cout << (char)196;
    cout << (char)217;

    DrawStatusBar(" ENTER=Restart Computer");

    int totalSeconds = 15;
    int totalMs = totalSeconds * 1000;
    DWORD startMs = GetTickCount();

    while (true) {
        DWORD elapsedMs = GetTickCount() - startMs;
        if (elapsedMs >= (DWORD)totalMs) break;

        int remainingSec = totalSeconds - (int)(elapsedMs / 1000);
        GotoXY(boxX + 10, 15);
        if (remainingSec >= 10)
            cout << "Your computer will reboot in " << remainingSec << " seconds...";
        else
            cout << "Your computer will reboot in " << remainingSec << " seconds..";

        int fillLen = ((15 - remainingSec) * barW) / totalSeconds;

        GotoXY(barX, barY);
        SetConsoleTextAttribute(hOut, 0x1C);
        for (int i = 0; i < fillLen; i++) {
            cout << (char)219;
        }
        SetConsoleTextAttribute(hOut, 0x17);

        if (_kbhit()) {
            int key = _getch();
            if (key == 13) break;
        }

        Sleep(100);
    }

    DrawStatusBar(" Restarting computer...");
    RunCommand("wpeutil reboot");
    Sleep(1000);
    exit(0);
}

// ==================== 5. Controller ====================
bool WaitForKey(DWORD timeoutMs, int targetExtKey) {
    DWORD start = GetTickCount();
    while (GetTickCount() - start < timeoutMs) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 0 || ch == 224) {
                int ext = _getch();
                if (ext == targetExtKey) {
                    return true;
                }
            }
        }
        Sleep(20);
    }
    return false;
}

void mainPageKey() {
    while (true) {
        int key = _getch();
        if (key == 13) {
            LicensePage();
            LicensePageKey();
            break;
        }
        else if (key == 'r' || key == 'R') {
            openRecoveryConsoleCMD();
            break;
        }
        else if (key == 0 || key == 224) {
            int fnKey = _getch();
            if (fnKey == 61) {
                if (showQuitConfirmPopup()) {
                    showRebootCountdownPage(false);
                }
                else {
                    mainPage();
                }
            }
        }
    }
}

bool confirmDeletePartitionKey() {
    while (true) {
        int key = _getch();
        if (key == 'l' || key == 'L') {
            return true;
        }
        else if (key == 27) {
            return false;
        }
    }
}

void runChooseHardDriveProcess() {
    while (true) {
        chooseHardDrive();
        vector<PartitionInfo> partitions = ScanPartitions();
        if (partitions.empty()) {
            partitions.push_back({ "C:", "RAW", 102400, 0 });
        }

        int partIndex = 0;

        while (true) {
            for (size_t p = 0; p < partitions.size() && p < 7; p++) {
                DrawPartitionItem(4, 14 + (short)p, 72, partitions[p], (p == partIndex));
            }

            int partKey = _getch();
            if (partKey == 0 || partKey == 224) {
                int extPartKey = _getch();
                if (extPartKey == 72 && partIndex > 0) {
                    partIndex--;
                }
                else if (extPartKey == 80 && partIndex < (int)partitions.size() - 1) {
                    partIndex++;
                }
                else if (extPartKey == 61) { 
                    if (showQuitConfirmPopup()) {
                        showRebootCountdownPage(false); 
                        return;
                    }
                    else {
                        chooseHardDrive(); 
                    }
                }
            }
            else if (partKey == 13) {
                PartitionInfo selectedPart = partitions[partIndex];
                int formatChoice = notArrangedDriveKey(selectedPart);

                if (formatChoice != -1) {
                    bool doFormat = true;
                    bool isQuick = true;
                    string fsType = "ntfs";

                    if (selectedPart.totalMB > 32768) {
                        if (formatChoice == 0) { isQuick = true; fsType = "ntfs"; }
                        else if (formatChoice == 1) { isQuick = false; fsType = "ntfs"; }
                        else if (formatChoice == 2) { doFormat = false; }
                    }
                    else {
                        if (formatChoice == 0) { isQuick = true; fsType = "ntfs"; }
                        else if (formatChoice == 1) { isQuick = true; fsType = "fat32"; }
                        else if (formatChoice == 2) { isQuick = false; fsType = "ntfs"; }
                        else if (formatChoice == 3) { isQuick = false; fsType = "fat32"; }
                        else if (formatChoice == 4) { doFormat = false; }
                    }

                    if (doFormat) {
                        showFormattingProgressPage(selectedPart, 20);
                        string driveLetterOnly = selectedPart.driveLetter.substr(0, 1);
                        string script = "select volume " + driveLetterOnly + ":\n";
                        script += "format fs=" + fsType + (isQuick ? " quick" : "") + " label=\"System\" override\n";
                        script += "active\n";
                        RunDiskpartScript(script);
                        showFormattingProgressPage(selectedPart, 100);
                        Sleep(500);
                    }

                    while (true) {
                        vector<PartitionInfo> currentParts = ScanPartitions();
                        bool foundRaw = false;

                        for (const auto& part : currentParts) {
                            if (part.driveLetter.substr(0, 1) != selectedPart.driveLetter.substr(0, 1) &&
                                (part.fsType == "RAW" || part.fsType.empty())) {

                                foundRaw = true;
                                showFormattingProgressPage(part, 20);

                                string rawDrive = part.driveLetter.substr(0, 1);
                                string script = "select volume " + rawDrive + ":\n";
                                script += "format fs=ntfs quick override\n";
                                RunDiskpartScript(script);

                                showFormattingProgressPage(part, 100);
                                Sleep(300);
                                break;
                            }
                        }
                        if (!foundRaw) break;
                    }

                    string targetDrive = selectedPart.driveLetter.substr(0, 1);
                    bool success = ApplyImageToTarget(targetDrive);

                    if (success) {
                        showSavingSettings(targetDrive);
                        showRebootCountdownPage(true);
                    }
                    else {
                        showRebootCountdownPage(false);
                    }
                }
                break;
            }

            else if (partKey == 'd' || partKey == 'D') {
                PartitionInfo selectedPart = partitions[partIndex];
                bool proceedToDeleteConfirm = true;

                if (HasInstalledOS(selectedPart.driveLetter)) {

                    deleteSystemPartitionWarnPage();
                    proceedToDeleteConfirm = deleteSystemPartitionWarnKey();
                }

                if (proceedToDeleteConfirm) {
                    confirmDeletePartitionPage(selectedPart);

                    if (confirmDeletePartitionKey()) {
                        string driveLetterOnly = selectedPart.driveLetter.substr(0, 1);
                        string script = "select volume " + driveLetterOnly + ":\n";
                        script += "delete partition override\n";

                        RunDiskpartScript(script);
                        break;
                    }
                }
                break;
            }
            else if (partKey == 'c' || partKey == 'C') {
                ULONGLONG diskTotalMB = 10237;
                ULONGLONG maxAvailableMB = 10229;

                ULONGLONG newSizeMB = createPartitionKey(diskTotalMB, maxAvailableMB);

                if (newSizeMB > 0) {
                    string script = "select disk 0\n";
                    script += "create partition primary size=" + to_string(newSizeMB) + "\n";
                    script += "assign\n";

                    RunDiskpartScript(script);
                    break;
                }
                break;
            }
        }
    }
}

void LicensePageKey() {
    while (true) {
        int key = _getch();
        if (key == 0 || key == 224) {
            int extKey = _getch();
            if (extKey == 66) {
                vector<InstalledOS> osList = ScanInstalledOS();

                if (osList.empty()) {
                    runChooseHardDriveProcess();
                    break;
                }

                repOS();

                int selectedIndex = 0;
                while (true) {
                    for (size_t i = 0; i < osList.size() && i < 6; i++) {
                        DrawOSItem(4, 15 + (short)i, 72, osList[i].path, osList[i].name, (i == selectedIndex));
                    }

                    DrawStatusBar(" F3=Quit  R=Repair  ESC=Don't Repair");

                    int osKey = _getch();
                    if (osKey == 0 || osKey == 224) {
                        int extOsKey = _getch();
                        if (extOsKey == 72 && selectedIndex > 0) selectedIndex--;
                        if (extOsKey == 80 && selectedIndex < (int)osList.size() - 1) selectedIndex++;
                        if (extOsKey == 61) showRebootCountdownPage(false);
                    }
                    else if (osKey == 'r' || osKey == 'R') {
                        RunCommand("X:\\sources\\recovery\\startrep.exe");
                        break;
                    }
                    else if (osKey == 27) {
                        runChooseHardDriveProcess();
                        break;
                    }
                }
                break;
            }
        }
        else if (key == 27) {
            showRebootCountdownPage(false);
            break;
        }
    }
}

bool deleteSystemPartitionWarnKey() {
    while (true) {
        int key = _getch();
        if (key == 13) {
            return true;
        }
        else if (key == 27) {
            return false;
        }
    }
}

ULONGLONG createPartitionKey(ULONGLONG diskTotalMB, ULONGLONG maxMB) {
    string inputSize = to_string(maxMB);

    while (true) {
        createPartitionPage(diskTotalMB, maxMB, inputSize);

        int key = _getch();
        if (key == 13) {
            if (inputSize.empty()) continue;
            ULONGLONG val = stoull(inputSize);
            if (val >= 8 && val <= maxMB) {
                return val;
            }
        }
        else if (key == 27) {
            return 0;
        }
        else if (key == 8) {
            if (!inputSize.empty()) {
                inputSize.pop_back();
            }
        }
        else if (key >= '0' && key <= '9') {
            if (inputSize.length() < 6) {
                inputSize += (char)key;
            }
        }
    }
}

// ==================== 6. Application Sequence ====================
int main() {
    SetupRetroConsole();
    setupStart();
    mainPage();
    mainPageKey();

    system("pause >nul");
    return 0;
}