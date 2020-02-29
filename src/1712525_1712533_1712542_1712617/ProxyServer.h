#ifndef _PROXYSERVER_H_
#define _PROXYSERVER_H_

#pragma comment(lib, "Ws2_32.lib")

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <WinSock2.h>
#include <process.h>
#include <string>
#include <queue>
#include <fstream>
#include <ctime>
#include <vector>
#include <map>
using namespace std;

#define PROXY_PORT  8888 //Port cua ProxyServer
#define BUFSIZ 4096 //Kich co cua mang buffer
#define MAX_THREADS	50 //So luong thread toi da

//Ham load cac host nam trong file blacklist.conf vao vector<string>
//Ham tra ve vector<string> la mang gom cac host nam trong blacklist.conf
vector<string> loadBlackList();

//Ham kiem tra 1 host co nam trong danh sach bi cam hay khong?
bool isForbidden(const string& host);

//Ham kiem tra request co la phuong thuc GET hay khong?
bool isGetMethod(const string& request);

//Ham kiem tra request co la phuong thuc POST hay khong?
bool isPostMethod(const string& request);

//Ham lay thong tin ve WebServer tu request nhu host, port va URL
void getWebServer(const string& request, string& host, string& port, string& ulr);

//Ham nhan du lieu tu Socket Proxy_WebServer, duoc viet theo co che ra vao Overlapped
DWORD Receive(SOCKET& Proxy_WebServer, WSABUF& proxyRecvBuf, WSAOVERLAPPED& proxyRecvOverlapped, DWORD& Flag, DWORD timeOut, bool wait);

//Ham lay ra chuoi date thu file cache
//Ham tra ve true neu thanh cong, false neu that bai
bool getDateOfCache(fstream& fi, string& date);

//Ham kiem tra file cache timeout hay chua?
//(Dieu kien: file chua timeout khi ngay cung ngay voi he thong va gio chenh lech voi he thong <= 1000s
bool cacheIsTimeOut(fstream& fi);

//Ham nhan response va tra ve cho client thong qua Socket Client_ProxyServer
//Response co the lay file Cache hoac nhan tu Server (va ghi lai vao file cache)
void getResponse(const char* HostName, const char* request, const string& dir, SOCKET& Client_ProxyServer);

//Ham nhan Response tu Web Server va gui ve cho client (thong qua Socket Client_ProxyServer) dong thoi luu lai vao file cache (thong qua fstream fo)
void getResponseFromWebServer(const char* HostName, const char* request, SOCKET& Client_ProxyServer, fstream& fo);

//Ham lay tung Socket Client_ProxyServer trong hang doi ra de xu ly
//Nhan request tu Client sau do kiem tra request co tu host trong blacklist hay khong?
//Sau do gui lai response cho Client (ghi lai cache neu co)
//Ham duoc viet theo cau truc de xu dung cho da luong (ham _beginthreadex)
unsigned __stdcall requestThread(void *);

//Ham chay ProxyServer gom cac cong viec cua ProxyServer.
void runServer();

#endif