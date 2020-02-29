#include "ProxyServer.h"


queue<SOCKET> socketBuffer; //Hang doi cua cac Socket Client_ProxyServer
HANDLE QueueMutex; //Mutex dung de han che cac truy cap cung luc khi chay da luong

//chuoi tra ve response 403 Forbidden khi website bi chan (nam trong blackist)
const char* forbidden = "HTTP/1.1 403 Forbidden\r\n\r\n<HTML>\r\n<BODY>\r\n<H1>403 Forbidden</H1>\r\n<H2>You don't have permission to access this server</H2>\r\n</BODY></HTML>\r\n";
//Load cac host nam trong file blacklist.conf vao vector<string> BlackList
vector<string> BlackList = loadBlackList();
//canCache dung de danh dau file cache nay co the su dung duoc khong (Khong the dung duoc khi co Thread khac dang dung file cache nay)
map<const char*, bool> canCache;


vector<string> loadBlackList()
{
	vector<string> v;
	fstream f;
	f.open("blacklist.conf", ios_base::in);
	if (f.is_open()) {
		while (!f.eof()) {
			string s;
			f >> s;
			v.push_back(s); //Doc tung dong va push vao vector
		}
		f.close();
	}
	return v;
}


bool isForbidden(const string& host)
{
	for (auto s : BlackList) { //Duyet qua tung phan tu trong vector
		if (host == s) //Neu host thuoc vector BlackList
			return true;
	}
	return false;
}


bool isGetMethod(const string& request)
{
	if (request.length() < 3)
		return false;
	if ((request[0] == 'G') && (request[1] == 'E') && (request[2] == 'T'))
		return true;
	return false;
}


bool isPostMethod(const string& request)
{
	if (request.length() < 4)
		return false;
	if ((request[0] == 'P') && (request[1] == 'O') && (request[2] == 'S') && (request[3] == 'T'))
		return true;
	return false;
}


void getWebServer(const string& request, string& host, string& port, string& url)
{
	int start = request.find("Host:") + 6; //vi tri bat dau cua host
	int end = request.substr(start).find("\r\n"); //vi tri ket thuc (so luong ky tu) cua host
	int f = (request.substr(start, end)).find(':');
	if (f == string::npos || f == -1) { //Neu khong co dau ':' thi port mac dinh la 80
		host = request.substr(start, end);
		port = "80";
	}
	else { //Nguoc lai, neu co ':' thi port sau dau ':'
		host = request.substr(start, f);
		port = request.substr(start + f + 1, end - (f + 1));
	}

	//Lay URL nham muc dich dat ten cho file cache
	int startURL = request.find(' ') + 1;
	int endURL = request.substr(startURL).find(' ');
	url = request.substr(startURL, endURL);
}


DWORD Receive(SOCKET& Proxy_WebServer, WSABUF& proxyRecvBuf, WSAOVERLAPPED& proxyRecvOverlapped, DWORD& Flag, DWORD timeOut, bool wait)
{
	DWORD nRead = 0;

	//Nhan du lieu
	int ret = WSARecv(Proxy_WebServer, &proxyRecvBuf, 1, &nRead, &Flag, &proxyRecvOverlapped, NULL);

	if (ret == SOCKET_ERROR && (WSAGetLastError() != WSA_IO_PENDING))
	{
		cout << "Error when receiving" << endl;
		return SOCKET_ERROR;
	}
	//Doi tin hieu
	ret = WSAWaitForMultipleEvents(1, &proxyRecvOverlapped.hEvent, TRUE, timeOut, TRUE);

	if (ret == WAIT_FAILED)
	{
		cout << "Wait failed" << endl;
		return SOCKET_ERROR;
	}
	//Nhan ket qua
	ret = WSAGetOverlappedResult(Proxy_WebServer, &proxyRecvOverlapped, &nRead, wait, &Flag);

	return nRead;
}

bool getDateOfCache(fstream& fi, string& date)
{
	char buf[1024];
	while (!fi.eof()) {
		fi.read((char*)buf, 1024);
		string s(buf);
		int f = s.find("Date:");
		if (f != string::npos) {
			int end = s.substr(f + 6).find(" GMT\n");
			date = s.substr(f + 6, end); //Lay ra chuoi Date
			fi.seekg(ios::beg);
			return true;
		}
	}
	fi.seekg(ios::beg);
	return false;
}

bool cacheIsTimeOut(fstream& fi)
{
	string date;
	if (getDateOfCache(fi, date) == false) //lay ngay gio theo file response luu trong cache
		return true;

	time_t current = time(0);
	tm *gmt = gmtime(&current);
	char* date_current = asctime(gmt); //chuoi ngay gio cua he thong (may tinh) theo GMT

	//tach chuoi ngay gio cua date_current ra tung thanh phan
	char day_cur[4], month_cur[4];
	int date_cur, year_cur, hour_cur, minute_cur, seconds_cur;
	sscanf(date_current, "%[^ ] %[^ ] %d %d:%d:%d %d", &day_cur, &month_cur, &date_cur, &hour_cur, &minute_cur, &seconds_cur, &year_cur);
	double time_cur = hour_cur * 3600 + minute_cur * 60 + seconds_cur;

	//tach chuoi ngay gio cua date ra tung thanh phan
	char day_cache[4], month_cache[4];
	int date_cache, year_cache, hour_cache, minute_cache, seconds_cache;
	sscanf(date.c_str(), "%[^,], %d %[^ ] %d %d:%d:%d GMT", &day_cache, &date_cache, &month_cache, &year_cache, &hour_cache, &minute_cache, &seconds_cache);
	double time_cache = hour_cache * 3600 + minute_cache * 60 + seconds_cache;

	//Timeout khi khac ngay thang, neu trong cung 1 ngay ma thoi gian chenh lech nhau 1000s thi TimeOut
	if ((year_cur != year_cache) || (string(month_cur) != string(month_cache)) || (date_cur != date_cache) || (abs(time_cur - time_cache) > 1000))
		return true;
	return false;
}

void getResponse(const char* HostName, const char* request, const string& url, SOCKET& Client_ProxyServer)
{
	//Neu la PostMethod thi khong cache lai ma phai gui va nhan response truc tiep tu WebServer
	bool IsPost = isPostMethod(string(request));
	if (IsPost) {
		fstream fo;
		getResponseFromWebServer(HostName, request, Client_ProxyServer, fo); //Nhan respone tu WebServer
		return;
	}

	//Duong dan ten file cache tuong ung voi 1 url co 1 file rieng
	string dirFile = string(HostName) + '_' + url + "_.cache";

	//Thay cac ky tu dac biet de tao ten cho file cache
	for (int i = 0; i < dirFile.length(); i++) {
		if ((dirFile[i] == '\\') || (dirFile[i] == '/') || (dirFile[i] == ':') || (dirFile[i] == '*') || (dirFile[i] == '?') ||
			(dirFile[i] == '"') || (dirFile[i] == '<') || (dirFile[i] == '>') || (dirFile[i] == '|'))
			dirFile[i] = '_';
	}

	dirFile = "cache\\" + dirFile;

	//Danh dau file nay co the su dung duoc
	canCache.insert({ dirFile.c_str(),true });


		if (canCache[dirFile.c_str()]) { //Neu file co the su dung duoc (dong nghia khong co thread nao truy cap den file nay)
			fstream fi;
			fi.open(dirFile.c_str(), ios_base::in | ios_base::binary);
			canCache[dirFile.c_str()] = false; //Danh dau la dang su dung, khong de thread khac cung truy cap vao
			if (fi.is_open()) { //Neu file co the mo duoc
				if ((cacheIsTimeOut(fi) == false)) { //Neu cache chua timeout
					cout << "File" <<dirFile<<" da ton tai ---------------------------------------------------------------\n";
					char c[1025];
					fi.seekg(ios::beg);
					while (!fi.eof()) {
						memset(c, 0, sizeof(c));
						fi.read(c, 1024);
						cout << c;
						int nRead = strlen(c);
						if (nRead <= 0)
							break;
						//Gui du lieu doc duoc tu cache xuong cho client
						send(Client_ProxyServer, c, 1024, 0);
					}
					fi.close();
					canCache[dirFile.c_str()] = true; //Danh dau co the su dung duoc sau khi da dong file
					return;
				}
			}
			fi.close();
			canCache[dirFile.c_str()] = true;

			remove(dirFile.c_str());
			fstream fo;
			fo.open(dirFile.c_str(), ios_base::out | ios_base::binary);
			canCache[dirFile.c_str()] = false;
			getResponseFromWebServer(HostName, request, Client_ProxyServer, fo); //Nhan respone tu WebServer dong thoi cache lai
			fo.close();
			canCache[dirFile.c_str()] = true;

			return;
		}
		return;
}

void getResponseFromWebServer(const char* HostName, const char* request, SOCKET& Client_ProxyServer, fstream& fo)
{
	SOCKET Proxy_WebServer;
	SOCKADDR_IN clientAddr;
	struct hostent *pHost;

	//Tao socket Proxy_WebServer (voi co overlapped duoc bat)
	Proxy_WebServer = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	pHost = gethostbyname(HostName);

	memset(&clientAddr, 0, sizeof(clientAddr));

	int addrLen = sizeof(clientAddr);

	//Khoi tao cau truc SOCKADDR_IN
	clientAddr.sin_family = AF_INET;
	memcpy(&clientAddr.sin_addr, pHost->h_addr, pHost->h_length);
	clientAddr.sin_port = htons(80);

	//Ket noi den WebServer thong qua socket Proxy_WebServer
	if (connect(Proxy_WebServer, (SOCKADDR *)&clientAddr, sizeof(SOCKADDR_IN)) != 0)
	{
		closesocket(Proxy_WebServer);
		return;
	}

	WSAOVERLAPPED SendOverlapped;
	DWORD Flag = 0;
	DWORD nSend = 0;
	WSABUF clientRequestBuf;
	WSAEVENT sendEvent[1];

	//Thiet lap cho clientRequestBuf
	clientRequestBuf.buf = (char*)request;
	clientRequestBuf.len = strlen(request);

	//Thiet lap cau truc SendOverlapped
	sendEvent[0] = WSACreateEvent(); //Tao doi tuong su kien
	SendOverlapped.hEvent = sendEvent[0];

	//Gui request len Web Server
	int ret = WSASend(Proxy_WebServer, &clientRequestBuf, 1, &nSend, Flag, &SendOverlapped, NULL);

	if (ret == SOCKET_ERROR && (WSAGetLastError() != WSA_IO_PENDING))
	{
		cout << "Error when receiving" << endl;
		return;
	}

	DWORD nRead = 0;
	char buf[BUFSIZ + 1];
	char sendBuf[BUFSIZ + 1];
	memset(buf, 0, sizeof(buf));
	memset(sendBuf, 0, sizeof(sendBuf));

	WSAOVERLAPPED proxyRecvOverlapped;
	WSABUF proxyRecvBuf;
	WSAEVENT proxyRecvEvent[1];
	//Thiet lap proxyRecvBuf
	proxyRecvBuf.buf = buf;
	proxyRecvBuf.len = sizeof(buf);


	//Thiet lap cau truc proxyRecvOverlapped
	proxyRecvEvent[0] = WSACreateEvent();
	proxyRecvOverlapped.hEvent = proxyRecvEvent[0];

	//Doi su kien sendEvent
	WSAWaitForMultipleEvents(1, sendEvent, FALSE, WSA_INFINITE, FALSE);

	ret = WSAGetOverlappedResult(Proxy_WebServer, &SendOverlapped, &nSend, FALSE, &Flag);

	//Nhan response tu Web Server
	nRead = recv(Proxy_WebServer, proxyRecvBuf.buf, BUFSIZ, 0);

	//Neu file request la POST hoac file Response khong cho cache thi khong cache lai
	bool IsPost = isPostMethod(string(request));
	bool IsNoCache = (strstr(buf, "Cache-Control: no-cache") != NULL);

	do
	{
		memcpy(sendBuf, buf, sizeof(buf));
		if ((!IsPost) && (!IsNoCache))
			fo.write((char*)buf, nRead);
		send(Client_ProxyServer, sendBuf, nRead, 0); //chuyen tiep xuong cho client

		memset(buf, 0, sizeof(buf));
		//Tiep tuc nhan response tu Web Server voi thoi gian timeout = 60s
		nRead = Receive(Proxy_WebServer, proxyRecvBuf, proxyRecvOverlapped, Flag, 60, FALSE);

		if ((nRead <= 0) || (nRead == SOCKET_ERROR))
		{
			cout << "Close socket" << endl;
			break;
		}

	} while (true);

	//Dong socket
	cout << "Close socket" << endl;
	shutdown(Proxy_WebServer, 2);
	closesocket(Proxy_WebServer);

	return;
}


unsigned __stdcall requestThread(void *)
{
	while (true)
	{
		SOCKET Client_Proxy;

		//Doi tung Mutex
		WaitForSingleObject(QueueMutex, INFINITE);

		if (socketBuffer.size() < 1)
		{
			ReleaseMutex(QueueMutex);
			continue;
		}

		try
		{
			Client_Proxy = socketBuffer.front();
			socketBuffer.pop();
			ReleaseMutex(QueueMutex);
		}
		catch (exception)
		{
			ReleaseMutex(QueueMutex);
			continue;
		}

		try {
			char buf[BUFSIZ + 1];
			memset(buf, 0, sizeof(buf));

			string data = "";

			//Nhan request tu client
			int recvLen = recv(Client_Proxy, buf, BUFSIZ, 0);
			if (recvLen <= 0)
			{
				return 0;
			}
			data += buf;

			while (recvLen >= BUFSIZ)
			{
				memset(buf, 0, sizeof(buf));
				recvLen = recv(Client_Proxy, buf, BUFSIZ, 0);
				data += buf;
			}

			string HostName;
			string Port;
			string URL;
			getWebServer(data, HostName, Port, URL);

			if ((Port == "80") && ((isGetMethod(data)) || (isPostMethod(data)))) { //Chi ho tro HTTP (Port 80), GET va POST
				cout << data << endl;

				if (isForbidden(HostName)) //Neu nam trong balcklist
					send(Client_Proxy, forbidden, strlen(forbidden), 0); //Gui ve 403 forbidden
				else
					getResponse(HostName.c_str(), data.c_str(), URL, Client_Proxy);
			}
			shutdown(Client_Proxy, 2);
			closesocket(Client_Proxy);
		}
		catch (exception)
		{
			shutdown(Client_Proxy, 2);
			closesocket(Client_Proxy);
		}
	}
	return 1;

}

void runServer()
{
	WSAData wsaData;
	//Khoi tao Winsock2.2
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		cout << "Init failed" << endl;
		return;
	}


	SOCKET ProxyServer, Client_Proxy;
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));

	//Tao socket ProxyServer
	ProxyServer = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	if (ProxyServer == INVALID_SOCKET)
	{
		cout << "Invalid socket" << endl;
		return;
	}

	//Khoi tao cau truc SOCKADDR_IN cua ProxyServer, doi ket noi o port: PROXY_PORT
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PROXY_PORT);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	//Bind socket cua ProxyServer
	if (bind(ProxyServer, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		cout << "Cannot bind to socket" << endl;
		return;
	}

	//Chuyen sang trang thai doi ket noi (listen)
	if (listen(ProxyServer, 25))
	{
		cout << "Listen failed" << endl;
		return;
	}

	QueueMutex = CreateMutex(NULL, FALSE, NULL);
	const int ThreadNumber = MAX_THREADS;
	HANDLE threads[ThreadNumber];
	unsigned int threadId[ThreadNumber];

	//Khoi tao va chay cac luong
	for (int i = 0; i < ThreadNumber; ++i)
		threads[i] = (HANDLE)_beginthreadex(NULL, 0, requestThread, NULL, 0, &threadId[i]);

	//Lan luot nhan cac yeu cau va cho vao hang doi (QueueMutex)
	while (true)
	{
		Client_Proxy = accept(ProxyServer, NULL, NULL);
		cout << "Accept" << endl;
		WaitForSingleObject(QueueMutex, INFINITE);
		socketBuffer.push(Client_Proxy);
		ReleaseMutex(QueueMutex);
	}

	//Dong socket ProxyServer va giai phong WinSock
	shutdown(ProxyServer, 2); //Dong ket noi
	closesocket(ProxyServer);
	WSACleanup();
}